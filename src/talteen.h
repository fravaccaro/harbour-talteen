#ifndef TALTEEN_H
#define TALTEEN_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class Talteen : public QObject
{
    Q_OBJECT

public:
    explicit Talteen(QObject *parent = nullptr);

    Q_INVOKABLE QString getSdCardPath();

    Q_INVOKABLE QVariantList getBackupFiles();
    Q_INVOKABLE bool deleteBackup(const QString &filePath);

    Q_INVOKABLE void startBackup(const QVariantMap &options);

    // Validate and read the YAML
    Q_INVOKABLE void analyzeArchive(const QString &backupFile);

    // Run the restore with user choices
    Q_INVOKABLE void executeRestore(const QString &backupFile, const QVariantMap &selectedOptions);

    Q_INVOKABLE qint64 getFreeSpace(bool onSdCard);

signals:
    void backupFinished(bool success, const QString &message);

    void archiveAnalyzed(bool isValid, const QString &message, const QVariantMap &metadata);

    void restoreFinished(bool success, const QString &message);
};

#endif // TALTEEN_H