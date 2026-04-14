#include "networktransfer.h"
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
            emit statusChanged(tr("Search stopped"));
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
        emit statusChanged(tr("Error: cannot start listening"));
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
        emit statusChanged(tr("Listening stopped"));
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
        emit statusChanged(tr("Searching for nearby devices..."));
    }
    else
    {
        emit statusChanged(tr("Cannot start search"));
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
        emit statusChanged(tr("Searching stopped"));
    }
}

void NetworkTransfer::sendFile(QString targetIp, int port, QString filePath)
{
    socket = new QTcpSocket(this);
    file = new QFile(filePath);

    if (!file->open(QIODevice::ReadOnly))
    {
        emit statusChanged(tr("Error: cannot open the file"));
        file->deleteLater();
        socket->deleteLater();
        emit progressChanged(0.0);
        return;
    }

    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName();

    totalBytes = fileInfo.size();
    processedBytes = 0;
    emit progressChanged(0.01);

    socket->connectToHost(targetIp, port);

    connect(socket, &QTcpSocket::connected, this, [this, fileName]()
            {
        emit statusChanged(tr("Accept on the other device. Waiting..."));
        
        // ONLY send the header. Do NOT send the file yet!
        QString header = fileName + "|" + QString::number(totalBytes) + "\n";
        socket->write(header.toUtf8()); });

    // Listen for the receiver's answer ("OK" or "REJECT")
    connect(socket, &QTcpSocket::readyRead, this, [this]()
            {
        QByteArray answer = socket->readAll();
        if (answer.contains("OK")) {
            emit statusChanged(tr("Accepted! Sending..."));
            socket->write(file->read(65536)); // Start the loop!
        } else {
            emit statusChanged(tr("Transfer cancelled by receiver"));
            emit progressChanged(0.0);
            socket->disconnectFromHost();
        } });

    connect(socket, &QTcpSocket::bytesWritten, this, [this](qint64 bytes)
            {
        // Don't trigger the file read if we just wrote the header string
        if (file->pos() == 0) return; 

        if (!file->atEnd()) {
            socket->write(file->read(65536));
        } else if (socket->bytesToWrite() == 0) {
            socket->disconnectFromHost(); 
        }

        processedBytes += bytes;
        if (totalBytes > 0) {
            double progress = static_cast<double>(processedBytes) / static_cast<double>(totalBytes);
            if (progress > 1.0) progress = 1.0;
            emit progressChanged(progress);
        } });

    connect(socket, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error), this, [this](QAbstractSocket::SocketError)
            {
        emit statusChanged(tr("Connection error: %1").arg(socket->errorString()));
        emit progressChanged(0.0);
        if (file->isOpen()) file->close(); });

    connect(socket, &QTcpSocket::disconnected, this, [this]()
            {
        if (file->isOpen()) file->close();
        
        // Only declare success if we actually finished sending the bytes
        if (processedBytes >= totalBytes && totalBytes > 0) {
            emit progressChanged(1.0);
            emit statusChanged(tr("Backup sent!"));
        }
        
        file->deleteLater();
        socket->deleteLater(); });
}

void NetworkTransfer::acceptConnection()
{
    socket = server->nextPendingConnection();

    if (!socket)
    {
        qDebug() << "[FATAL ERROR] nextPendingConnection returned nullptr!";
        return;
    }

    qDebug() << "[DEBUG] Incoming connection accepted!";
    broadcastTimer->stop();
    file = new QFile(this);
    incomingData.clear();
    m_waitingForUser = false; // Reset the wait state

    totalBytes = 0;
    processedBytes = 0;
    emit progressChanged(0.01);

    connect(socket, &QTcpSocket::readyRead, this, [this]()
            {
        if (!file->isOpen()) {
            if (m_waitingForUser) return; // Do nothing until the user clicks Accept or Reject!

            incomingData.append(socket->readAll());
            int newlineIndex = incomingData.indexOf('\n');
            
            if (newlineIndex != -1) {
                QString header = QString::fromUtf8(incomingData.left(newlineIndex)).trimmed();
                qDebug() << "[DEBUG] Header received:" << header;
                
                m_pendingFileName = "incoming_backup.talteen";

                if (header.contains('|')) {
                    QStringList parts = header.split('|');
                    m_pendingFileName = parts[0];
                    totalBytes = parts[1].toLongLong();
                }

                m_pendingFileSize = totalBytes;
                
                // Save any extra bytes that accidentally arrived with the header
                incomingData = incomingData.mid(newlineIndex + 1);
                m_waitingForUser = true; 

                // ASK THE QML UI WHAT TO DO
                emit transferRequested(m_pendingFileName, m_pendingFileSize);

            } else if (incomingData.size() > 1024) {
                qDebug() << "[SECURITY WARNING] Header is too long! Aborting connection.";
                emit statusChanged(tr("Error: Invalid data format"));
                emit progressChanged(0.0);
                socket->disconnectFromHost();
                incomingData.clear(); 
                return;
            }
        } else {
            QByteArray newData = socket->readAll();
            file->write(newData);
            processedBytes += newData.size();
            
            if (totalBytes > 0) {
                double progress = static_cast<double>(processedBytes) / static_cast<double>(totalBytes);
                if (progress > 1.0) progress = 1.0;
                emit progressChanged(progress);
            }
        } });

    connect(socket, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error), this, [this](QAbstractSocket::SocketError err)
            {
        if (err == QAbstractSocket::RemoteHostClosedError) return;

        qDebug() << "[SOCKET ERROR]" << socket->errorString();
        emit statusChanged(tr("Network error!"));
        emit progressChanged(0.0);
        if (file->isOpen()) {
            file->close();
            file->remove();
        } });

    connect(socket, &QTcpSocket::disconnected, this, [this]()
            {
        qDebug() << "[DEBUG] Socket disconnected";
        if (file->isOpen()) {
            QString savedPath = file->fileName(); 
            file->close();

            if (totalBytes > 0 && processedBytes < totalBytes) {
                qDebug() << "[DEBUG] Incomplete transfer. Deleting file.";
                file->remove();
                emit statusChanged(tr("Error: connection lost. Cannot send backup"));
                emit progressChanged(0.0);
            } else {
                qDebug() << "[DEBUG] Transfer completed!";
                emit statusChanged(tr("Transfer completed!\nSaved in: %1").arg(savedPath));
                emit progressChanged(1.0);
            }
        }
        file->deleteLater();
        socket->deleteLater();
        stopReceiving(); });
}

void NetworkTransfer::acceptTransfer(bool useSdCard)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;

    // Save the user's choice right when they click "Accept"
    m_useSdCard = useSdCard;

    QString baseFolder;
    if (m_useSdCard)
    {
        baseFolder = getSdCardPath();
        if (baseFolder.isEmpty())
        {
            emit statusChanged(tr("Error: SD Card not found"));
            rejectTransfer();
            return;
        }
        baseFolder += "/TalteenBackup";
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
        emit statusChanged(tr("Not enough free space!"));
        rejectTransfer();
        return;
    }

    QString savePath = QDir(baseFolder).filePath(m_pendingFileName);
    file->setFileName(savePath);

    if (!file->open(QIODevice::WriteOnly))
    {
        emit statusChanged(tr("Error saving the file!"));
        rejectTransfer();
        return;
    }

    // Write any bytes we buffered while waiting for the user
    if (!incomingData.isEmpty())
    {
        file->write(incomingData);
        processedBytes += incomingData.size();
        incomingData.clear();
    }

    m_waitingForUser = false;
    totalBytes = m_pendingFileSize;
    emit statusChanged(tr("Receiving...\n%1").arg(m_pendingFileName));

    // TELL SENDER TO START
    socket->write("OK");
}

void NetworkTransfer::rejectTransfer()
{
    if (socket && socket->state() == QAbstractSocket::ConnectedState)
    {
        socket->write("REJECT");
        socket->disconnectFromHost();
    }
    m_waitingForUser = false;
    incomingData.clear();
    emit statusChanged(tr("Sending cancelled"));
    emit progressChanged(0.0);
}

void NetworkTransfer::cancelTransfer()
{
    // If the socket is currently connected and sending data, force it to disconnect!
    if (socket && socket->state() == QAbstractSocket::ConnectedState)
    {
        socket->disconnectFromHost();
        emit statusChanged(tr("Transfer cancelled"));
        emit progressChanged(0.0);
    }
}