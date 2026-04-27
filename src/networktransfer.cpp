#include "networktransfer.h"
#include <memory>
#include <QDebug>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QDir>

NetworkTransfer::NetworkTransfer(QObject *parent)
    : QObject(parent),
      server(new QTcpServer(this)),
      socket(nullptr),
      file(nullptr),
      udpSocket(new QUdpSocket(this)),
      broadcastTimer(new QTimer(this)),
      broadcastCount(0),
      discoveryTimeoutTimer(new QTimer(this))
{
    broadcastTimer->setInterval(2000);

    connect(broadcastTimer, &QTimer::timeout, this, [this]()
            {
        QByteArray message = "SAILFISH_FILE_TRANSFER_READY";
        udpSocket->writeDatagram(message, QHostAddress::Broadcast, 45454);
        
        broadcastCount++;
        
        if (broadcastCount >= 30) {
            stopReceiving(); 
        } });

    discoveryTimeoutTimer->setSingleShot(true);
    connect(discoveryTimeoutTimer, &QTimer::timeout, this, [this]()
            {
        if (m_isDiscovering) {
            stopDiscovery();
            emit statusChanged(tr("Search off"));
        } });

    connect(server, &QTcpServer::newConnection, this, &NetworkTransfer::acceptConnection);
}

QString NetworkTransfer::getSdCardPath()
{
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes())
    {
        if (storage.isValid() && storage.isReady() && !storage.isReadOnly())
        {
            QString path = storage.rootPath();
            if (path.startsWith("/run/media/"))
            {
                return path;
            }
        }
    }
    return "";
}

bool NetworkTransfer::hasSdCard()
{
    return !getSdCardPath().isEmpty();
}

qint64 NetworkTransfer::getFreeSpace(bool onSdCard)
{
    QString path = onSdCard ? getSdCardPath() : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (path.isEmpty())
        return 0;

    QStorageInfo storage(path);
    return storage.bytesAvailable();
}

void NetworkTransfer::startReceiving(int port)
{
    if (server->isListening())
        return;

    if (server->listen(QHostAddress::Any, port))
    {
        broadcastCount = 0;
        broadcastTimer->start();
        emit isListeningChanged();
        emit statusChanged(tr("Waiting for connections..."));
    }
    else
    {
        emit statusChanged(tr("Unable to start receive mode"));
    }
}

void NetworkTransfer::stopReceiving()
{
    if (server->isListening())
    {
        server->close();
        broadcastTimer->stop();
        broadcastCount = 0;
        emit isListeningChanged();
        emit statusChanged(tr("Receive mode off"));
    }
}

void NetworkTransfer::discoverDevices()
{
    if (m_isDiscovering)
        return;

    udpSocket->close();
    udpSocket->disconnect();

    if (udpSocket->bind(45454, QUdpSocket::ShareAddress))
    {
        connect(udpSocket, &QUdpSocket::readyRead, this, [this]()
                {
            while (udpSocket->hasPendingDatagrams()) {
                qint64 size = udpSocket->pendingDatagramSize();
                if (size <= 0) {
                    udpSocket->readDatagram(nullptr, 0);
                    continue; 
                }
                QByteArray buffer;
                buffer.resize(int(size));
                QHostAddress senderIp;
                quint16 senderPort;
                udpSocket->readDatagram(buffer.data(), buffer.size(), &senderIp, &senderPort);
                if (QNetworkInterface::allAddresses().contains(senderIp)) continue; 
                
                if (buffer == "SAILFISH_FILE_TRANSFER_READY") {
                    QString foundIp = senderIp.toString();
                    foundIp.replace("::ffff:", ""); 
                    emit deviceDiscovered(foundIp);
                }
            } });

        m_isDiscovering = true;
        discoveryTimeoutTimer->start(60000);

        emit isDiscoveringChanged();
        emit statusChanged(tr("Looking for nearby devices..."));
    }
    else
    {
        emit statusChanged(tr("Unable to start search"));
    }
}

void NetworkTransfer::stopDiscovery()
{
    if (m_isDiscovering)
    {
        udpSocket->close();
        udpSocket->disconnect();
        discoveryTimeoutTimer->stop();

        m_isDiscovering = false;
        emit isDiscoveringChanged();
        emit statusChanged(tr("Search off"));
    }
}

// Updated signature to include fileLabel
void NetworkTransfer::sendFile(QString targetIp, int port, QString filePath, QString fileLabel)
{

    m_cancelledByUser = false;

    // Prevent Concurrency / Button Spamming
    if (socket && socket->state() != QAbstractSocket::UnconnectedState)
    {
        qDebug() << "[WARNING] Send requested but a transfer is already in progress!";
        return;
    }

    // Safely clean up old pointers if they survived a weird state
    if (socket)
    {
        socket->deleteLater();
        socket = nullptr;
    }
    if (file)
    {
        file->deleteLater();
        file = nullptr;
    }

    socket = new QTcpSocket(this);
    file = new QFile(this);
    file->setFileName(filePath);

    if (!file->open(QIODevice::ReadOnly))
    {
        emit statusChanged(tr("Unable to open file"));
        file->deleteLater();
        file = nullptr;
        socket->deleteLater();
        socket = nullptr;
        emit progressChanged(0.0);
        return;
    }

    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName();

    totalBytes = fileInfo.size();

    // Prevent the 0-byte Infinite Hang
    if (totalBytes == 0)
    {
        emit statusChanged(tr("Error: unable to send empty file"));
        file->close();
        file->deleteLater();
        file = nullptr;
        socket->deleteLater();
        socket = nullptr;
        emit progressChanged(0.0);
        return;
    }

    processedBytes = 0;
    emit progressChanged(0.01);

    socket->connectToHost(targetIp, port);

    // Add the variables to the lambda capture, and constructed the 3-part header
    connect(socket, &QTcpSocket::connected, this, [this, fileName, fileLabel, fileInfo]()
            {
        emit statusChanged(tr("Waiting for the other device to accept..."));
        
        QString labelToSend = fileLabel.isEmpty() ? fileInfo.completeBaseName() : fileLabel;
        QString header = fileName + "|" + QString::number(totalBytes) + "|" + labelToSend + "\n";
        socket->write(header.toUtf8()); });

    // Use a Smart Pointer to prevent memory leaks
    auto responseBuffer = std::make_shared<QByteArray>();

    connect(socket, &QTcpSocket::readyRead, this, [this, responseBuffer]()
            {
        responseBuffer->append(socket->readAll());
        
        if (responseBuffer->contains("OK")) {
            emit statusChanged(tr("Accepted. Sending..."));
            socket->write(file->read(65536)); // Start the loop!
            responseBuffer->clear();
        } else if (responseBuffer->contains("REJECT")) {
            emit statusChanged(tr("Transfer declined by receiver"));
            emit progressChanged(0.0);
            socket->disconnectFromHost();
            responseBuffer->clear();
        } });

    connect(socket, &QTcpSocket::bytesWritten, this, [this](qint64 bytes)
            {

                // If the user clicked stop, DO NOT read more of the file!
        if (m_cancelledByUser) return;
        // Don't trigger the file read if we just wrote the header string
        if (file->pos() == 0) return; 

        processedBytes += bytes;

        if (!file->atEnd()) {
            socket->write(file->read(65536));
        } else if (socket->bytesToWrite() == 0) {
            socket->disconnectFromHost(); 
        }

        if (totalBytes > 0) {
            double progress = static_cast<double>(processedBytes) / static_cast<double>(totalBytes);
            if (progress > 1.0) progress = 1.0;
            emit progressChanged(progress);
        } });

    connect(socket, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error), this, [this](QAbstractSocket::SocketError)
            {
        emit statusChanged(tr("Connection error: %1").arg(socket->errorString()));
        emit progressChanged(0.0);
        if (file && file->isOpen()) file->close(); 
        
        // Fix Memory Leak if it fails to connect
        if (socket->state() == QAbstractSocket::UnconnectedState) {
            if (file) { file->deleteLater(); file = nullptr; }
            if (socket) { socket->deleteLater(); socket = nullptr; }
        } });

    connect(socket, &QTcpSocket::disconnected, this, [this, responseBuffer]()
            {
        if (file && file->isOpen()) file->close();
        
        // Check for a last-millisecond REJECT before assuming connection loss
        if (socket->bytesAvailable() > 0) {
            responseBuffer->append(socket->readAll());
        }

        if (responseBuffer->contains("REJECT")) {
            emit statusChanged(tr("Transfer cancelled by receiver"));
            emit progressChanged(0.0);
        }
        else if (processedBytes >= totalBytes && totalBytes > 0) {
            emit progressChanged(1.0);
            emit statusChanged(tr("Backup sent"));
        } else if (!m_cancelledByUser) {
            emit statusChanged(tr("Connection lost. Unable to send backup"));
            emit progressChanged(0.0);
        }
        
        // Prevent Dangling Pointers
        if (file) { file->deleteLater(); file = nullptr; }
        if (socket) { socket->deleteLater(); socket = nullptr; } });
}

void NetworkTransfer::acceptConnection()
{
    QTcpSocket *incomingSocket = server->nextPendingConnection();
    if (!incomingSocket)
        return;

    // Prevent Concurrency Hijacking
    if (socket && (socket->state() == QAbstractSocket::ConnectedState || m_waitingForUser))
    {
        qDebug() << "[WARNING] Rejected concurrent connection attempt. Server is busy.";
        incomingSocket->disconnectFromHost();
        incomingSocket->deleteLater();
        return;
    }

    socket = incomingSocket;

    qDebug() << "[DEBUG] Incoming connection accepted!";
    broadcastTimer->stop();

    // Safely clean up old file if it somehow survived
    if (file)
        file->deleteLater();
    file = new QFile(this);

    incomingData.clear();
    m_waitingForUser = false;
    m_cancelledByUser = false;

    totalBytes = 0;
    processedBytes = 0;
    emit progressChanged(0.01);

    connect(socket, &QTcpSocket::readyRead, this, [this]()
            {
                if (!file->isOpen())
                {
                    if (m_waitingForUser)
                        return; // Do nothing until the user clicks Accept or Reject!

                    incomingData.append(socket->readAll());
                    int newlineIndex = incomingData.indexOf('\n');

                    if (newlineIndex != -1)
                    {
                        QString header = QString::fromUtf8(incomingData.left(newlineIndex)).trimmed();
                        qDebug() << "[DEBUG] Header received:" << header;

                        m_pendingFileName = "incoming_backup.talteen";
                        
                        // Parse the 3-part header and set a fallback label
                        QString m_pendingFileLabel = m_pendingFileName;

                        if (header.contains('|'))
                        {
                            QStringList parts = header.split('|');
                            m_pendingFileName = QFileInfo(parts[0]).fileName();
                            totalBytes = parts[1].toLongLong();
                            
                            if (parts.size() > 2) {
                                m_pendingFileLabel = parts[2];
                            }
                        }

                        m_pendingFileSize = totalBytes;

                        // Save any extra bytes that accidentally arrived with the header
                        incomingData = incomingData.mid(newlineIndex + 1);
                        m_waitingForUser = true;

                        // ASK THE QML UI WHAT TO DO
                        emit transferRequested(m_pendingFileName, m_pendingFileSize, m_pendingFileLabel);
                    }
                    else if (incomingData.size() > 1024)
                    {
                        qDebug() << "[SECURITY WARNING] Header is too long! Aborting connection.";
                        emit statusChanged(tr("Error: not a valid backup"));
                        emit progressChanged(0.0);
                        socket->disconnectFromHost();
                        incomingData.clear();
                        return;
                    }
                }
                else
                {
                    QByteArray newData = socket->readAll();

                    // --- SECURITY FIX: Prevent Malicious Storage Exhaustion ---
                    qint64 bytesRemaining = totalBytes - processedBytes;

                    if (newData.size() > bytesRemaining)
                    {
                        qDebug() << "[SECURITY WARNING] Sender sent more data than declared! Aborting.";
                        // Write only up to the agreed limit, then instantly sever the connection
                        file->write(newData.left(bytesRemaining));
                        processedBytes += bytesRemaining;
                        socket->disconnectFromHost();
                    }
                    else
                    {
                        file->write(newData);
                        processedBytes += newData.size();
                    }

                    if (totalBytes > 0)
                    {
                        double progress = static_cast<double>(processedBytes) / static_cast<double>(totalBytes);
                        if (progress > 1.0)
                            progress = 1.0;
                        emit progressChanged(progress);
                    }
                } });

    connect(socket, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error), this, [this](QAbstractSocket::SocketError err)
            {
        if (err == QAbstractSocket::RemoteHostClosedError) return;

        qDebug() << "[SOCKET ERROR]" << socket->errorString();
        emit statusChanged(tr("Network error"));
        emit progressChanged(0.0);
        if (file && file->isOpen()) {
            file->close();
            file->remove();
        } });

    connect(socket, &QTcpSocket::disconnected, this, [this]()
            {
                qDebug() << "[DEBUG] Socket disconnected";

                stopReceiving();

                // Handle sender dropping while prompt is open
                if (m_waitingForUser)
                {
                    m_waitingForUser = false;
                    incomingData.clear();
                    emit statusChanged(tr("Sender disconnected"));
                    emit progressChanged(0.0);
                    emit transferAborted(); // Tell the UI to hide the dialog
                }
                else if (file && file->isOpen())
                {
                    QString savedPath = file->fileName();
                    file->close();

                    if (totalBytes > 0 && processedBytes < totalBytes)
                    {
                        qDebug() << "[DEBUG] Incomplete transfer. Deleting file.";
                        file->remove();

                        // Prevent double-status if receiver cancelled
                        if (!m_cancelledByUser)
                        {
                            emit statusChanged(tr("Connection lost. Unable to receive backup"));
                        }
                        emit progressChanged(0.0);
                    }
                    else
                    {
                        qDebug() << "[DEBUG] Transfer complete!";
                        emit statusChanged(tr("Transfer complete"));
                        emit progressChanged(1.0);
                    }
                }

                // Prevent Dangling Pointers
                if (file)
                {
                    file->deleteLater();
                    file = nullptr;
                }
                if (socket)
                {
                    socket->deleteLater();
                    socket = nullptr;
                } });
}

void NetworkTransfer::acceptTransfer(bool useSdCard)
{

    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;

    // Prevent Double-Tapping the Accept button
    if (!m_waitingForUser)
        return;

    // Save the user's choice right when they click "Accept"
    m_useSdCard = useSdCard;

    QString baseFolder;
    if (m_useSdCard)
    {
        baseFolder = getSdCardPath();
        if (baseFolder.isEmpty())
        {
            emit statusChanged(tr("Error: SD card not found"));
            rejectTransfer();
            return;
        }
        baseFolder += "/harbour-talteen";
    }
    else
    {
        baseFolder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    // ENSURE THE FOLDER EXISTS BEFORE CHECKING SPACE OR SAVING
    QDir().mkpath(baseFolder);

    QStorageInfo storage(baseFolder);
    if (storage.bytesAvailable() < (m_pendingFileSize + 5242880))
    {
        emit statusChanged(tr("Not enough storage space"));
        rejectTransfer();
        return;
    }

    QString savePath = QDir(baseFolder).filePath(m_pendingFileName);
    file->setFileName(savePath);

    if (!file->open(QIODevice::WriteOnly))
    {
        emit statusChanged(tr("Error saving file"));
        rejectTransfer();
        return;
    }

    // Write any bytes we buffered while waiting for the user
    if (!incomingData.isEmpty())
    {
        // Bound the buffer write
        qint64 bytesRemaining = m_pendingFileSize;

        if (incomingData.size() > bytesRemaining)
        {
            qDebug() << "[SECURITY WARNING] Buffered data exceeded declared file size! Aborting.";
            file->write(incomingData.left(bytesRemaining));
            processedBytes += bytesRemaining;
            incomingData.clear();
            socket->disconnectFromHost();
            return; // Exit immediately, do NOT send "OK"
        }
        else
        {
            file->write(incomingData);
            processedBytes += incomingData.size();
            incomingData.clear();
        }
    }

    m_waitingForUser = false;
    totalBytes = m_pendingFileSize;
    emit statusChanged(tr("Receiving..."));

    // TELL SENDER TO START
    socket->write("OK");
}

void NetworkTransfer::rejectTransfer()
{

    qDebug() << "[DEBUG] rejectTransfer() triggered";
    if (socket && socket->state() != QAbstractSocket::UnconnectedState) // <-- FIX
    {
        m_cancelledByUser = true;
        if (socket->state() == QAbstractSocket::ConnectedState)
        {
            socket->write("REJECT");
            socket->flush();
        }
        socket->disconnectFromHost();
    }
    m_waitingForUser = false;
    incomingData.clear();
    emit statusChanged(tr("Sending cancelled"));
    emit progressChanged(0.0);
}

void NetworkTransfer::cancelTransfer()
{
    qDebug() << "[DEBUG] cancelTransfer() triggered";
    if (socket && socket->state() != QAbstractSocket::UnconnectedState) // <-- FIX
    {
        m_cancelledByUser = true;
        socket->disconnectFromHost();
        emit statusChanged(tr("Transfer cancelled"));
        emit progressChanged(0.0);
    }
}