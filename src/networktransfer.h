#ifndef NETWORKTRANSFER_H
#define NETWORKTRANSFER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QFile>
#include <QTimer>
#include <QStorageInfo>

class NetworkTransfer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isListening READ isListening NOTIFY isListeningChanged)
    Q_PROPERTY(bool isDiscovering READ isDiscovering NOTIFY isDiscoveringChanged)

public:
    explicit NetworkTransfer(QObject *parent = nullptr);

    bool isListening() const { return server->isListening(); }
    bool isDiscovering() const { return m_isDiscovering; }

    Q_INVOKABLE void startReceiving(int port);
    Q_INVOKABLE void stopReceiving();
    Q_INVOKABLE void discoverDevices();
    Q_INVOKABLE void sendFile(QString targetIp, int port, QString filePath);
    Q_INVOKABLE void acceptTransfer(bool useSdCard);
    Q_INVOKABLE void rejectTransfer();
    Q_INVOKABLE void cancelTransfer();

    Q_INVOKABLE void stopDiscovery();

    Q_INVOKABLE qint64 getFreeSpace(bool onSdCard);
    Q_INVOKABLE bool hasSdCard();

signals:
    void statusChanged(QString status);
    void deviceDiscovered(QString ipAddress);
    void isListeningChanged();
    void isDiscoveringChanged();
    void transferRequested(QString fileName, qint64 fileSize);
    void progressChanged(double progress);

private slots:
    void acceptConnection();

private:
    QTcpServer *server;
    QTcpSocket *socket;
    QFile *file;
    QUdpSocket *udpSocket;
    QTimer *broadcastTimer;
    int broadcastCount;

    QByteArray incomingData;
    qint64 totalBytes;
    qint64 processedBytes;

    bool m_isDiscovering = false;
    QTimer *discoveryTimeoutTimer;

    bool m_useSdCard = false;
    QString getSdCardPath();

    QString m_pendingFileName;
    qint64 m_pendingFileSize;
    bool m_waitingForUser = false;
};

#endif // NETWORKTRANSFER_H