#include "talteen.h"
#include "spawner.h"
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QSettings>

Talteen::Talteen(QObject *parent) : QObject(parent)
{
}

qint64 calculateDirSize(const QString &dirPath)
{
    qint64 size = 0;
    QDir dir(dirPath);

    QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::Dirs |
                                           QDir::NoDotAndDotDot | QDir::Hidden |
                                           QDir::System | QDir::NoSymLinks);
    for (const QFileInfo &fileInfo : list)
    {
        if (fileInfo.isDir())
        {
            size += calculateDirSize(fileInfo.absoluteFilePath());
        }
        else
        {
            size += fileInfo.size();
        }
    }
    return size;
}

QString Talteen::getSdCardPath()
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

qint64 Talteen::getFreeSpace(bool onSdCard)
{
    QString path = onSdCard ? getSdCardPath() : QDir::homePath();
    if (path.isEmpty())
        return 0;

    // Ensure the folder exists before checking space
    QDir().mkpath(path);

    QStorageInfo storage(path);
    return storage.bytesAvailable();
}

void Talteen::startBackup(const QVariantMap &options)
{
    QString homePath = QDir::homePath();

    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QString workDir = cachePath + "/workdir";

    QString dateTimeString = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm");

    QDir(workDir).removeRecursively();
    QDir().mkpath(workDir);

    bool hasAppdata = options.value("appdata").toBool();
    bool hasApporder = options.value("apporder").toBool();
    bool hasCalls = options.value("calls").toBool();
    bool hasMessages = options.value("messages").toBool();
    bool hasPictures = options.value("pictures").toBool();
    bool hasDocuments = options.value("documents").toBool();
    bool hasDownloads = options.value("downloads").toBool();
    bool hasMusic = options.value("music").toBool();
    bool hasVideos = options.value("videos").toBool();

    qint64 estimatedSize = 0;

    if (hasPictures)
        estimatedSize += calculateDirSize(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    if (hasDocuments)
        estimatedSize += calculateDirSize(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (hasDownloads)
        estimatedSize += calculateDirSize(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (hasMusic)
        estimatedSize += calculateDirSize(QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    if (hasVideos)
        estimatedSize += calculateDirSize(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));

    if (hasAppdata)
    {
        estimatedSize += calculateDirSize(homePath + "/.config");
        estimatedSize += calculateDirSize(homePath + "/.local");
    }

    QString destOption = options.value("destination").toString();
    QString targetFolder = (destOption == "internal" || destOption.isEmpty()) ? homePath : destOption;

    // Always ensure the directory exists before QStorageInfo checks it
    QDir().mkpath(targetFolder);

    QStorageInfo storage(targetFolder);
    qint64 freeSpace = storage.bytesAvailable();

    if (freeSpace < (estimatedSize + 104857600))
    {
        qDebug() << "[ERROR] Not enough free space for backup. Need approx:" << estimatedSize << "Have:" << freeSpace;
        emit backupFinished(false, tr("Not enough storage space to save the backup"));
        return;
    }

    qDebug() << "Start preparing backup...";

    QFile yamlFile(workDir + "/content.yaml");
    if (yamlFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&yamlFile);

        QString appVersion = "1.0.0";
        out << "version: \"" << appVersion << "\"\n";
        out << "time: \"" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\"\n";

        QString userLabel = options.value("label").toString().trimmed();
        QString baseFileName = "talteen_backup_" + dateTimeString;

        out << "label: \"" << (userLabel.isEmpty() ? baseFileName : userLabel) << "\"\n";

        QStringList allCategories = {
            "appdata", "apporder", "calls", "messages", "pictures",
            "documents", "downloads", "music", "videos"};

        for (const QString &category : allCategories)
        {
            bool isIncluded = options.value(category).toBool();
            out << category << ": " << (isIncluded ? "true" : "false") << "\n";
        }

        out << "EOF: true\n";
        yamlFile.close();
    }

    if (hasApporder)
    {
        QDir().mkpath(workDir + "/apporder");
        QFile::copy(homePath + "/.config/lipstick/applications.menu", workDir + "/apporder/applications.menu");
    }

    if (hasAppdata)
    {
        QDir().mkpath(workDir + "/appdata");
        QFile::link(homePath + "/.config", workDir + "/appdata/.config");
        QFile::link(homePath + "/.local", workDir + "/appdata/.local");
    }

    if (hasPictures)
        QFile::link(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), workDir + "/pictures");
    if (hasDocuments)
        QFile::link(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), workDir + "/documents");
    if (hasDownloads)
        QFile::link(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), workDir + "/downloads");
    if (hasMusic)
        QFile::link(QStandardPaths::writableLocation(QStandardPaths::MusicLocation), workDir + "/music");
    if (hasVideos)
        QFile::link(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation), workDir + "/videos");

    auto runTarStep = [=]()
    {
        QString destOption = options.value("destination").toString();
        QString backupFolder;

        // Use standard AppData location for internal, and a specific folder for SD Card
        if (destOption == "internal" || destOption.isEmpty())
        {
            backupFolder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        }
        else
        {
            backupFolder = destOption + "/harbour-talteen";
        }

        QDir().mkpath(backupFolder);

        QString finalDestination = backupFolder + "/talteen_backup_" + dateTimeString + ".talteen";

        QProcess *tarProcess = new QProcess(this);
        tarProcess->setWorkingDirectory(workDir);

        QStringList tarArgs;
        tarArgs << "-czhf" << finalDestination;

        if (hasAppdata)
        {
            QStringList excludePaths = {
                "appdata/.config/Jolla", "appdata/.config/QtProject", "appdata/.config/dconf",
                "appdata/.config/libaccounts-glib", "appdata/.config/lipstick", "appdata/.config/nemo",
                "appdata/.config/nemomobile", "appdata/.config/pulse", "appdata/.config/signond",
                "appdata/.config/systemd", "appdata/.config/tracker", "appdata/.config/user-dirs.dirs",
                "appdata/.config/user-dirs.locale", "appdata/.config/.sailfish-gallery-reindex",
                "appdata/.local/nemo-transferengine",
                "appdata/.local/share/ambienced", "appdata/.local/share/applications",
                "appdata/.local/share/commhistory", "appdata/.local/share/dbus-1",
                "appdata/.local/share/gsettings-data-convert", "appdata/.local/share/maliit-server",
                "*/.mozilla/lock",
                "*/.mozilla/.parentlock",
                "appdata/.local/share/system", "appdata/.local/share/systemd",
                "appdata/.local/share/telepathy", "appdata/.local/share/system/privilege/Contacts",
                "appdata/.local/share/tracker", "appdata/.local/share/xt9"};
            for (const QString &path : excludePaths)
            {
                tarArgs << "--exclude=" + path;
            }
        }

        tarArgs << "content.yaml";
        if (hasApporder)
            tarArgs << "apporder";
        if (hasCalls)
            tarArgs << "calls";
        if (hasMessages)
            tarArgs << "messages";
        if (hasAppdata)
            tarArgs << "appdata";
        if (hasPictures)
            tarArgs << "pictures";
        if (hasDocuments)
            tarArgs << "documents";
        if (hasDownloads)
            tarArgs << "downloads";
        if (hasMusic)
            tarArgs << "music";
        if (hasVideos)
            tarArgs << "videos";

        connect(tarProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus)
                {
                    QDir(workDir).removeRecursively();

                    if (exitCode == 0 || exitCode == 1)
                    {
                        qDebug() << "Backup saved in:" << finalDestination << "(Exit code:" << exitCode << ")";
                        emit backupFinished(true, tr("Backup saved successfully"));
                    }
                    else
                    {
                        qDebug() << "Error tar (Exit code:" << exitCode << "):" << tarProcess->readAllStandardError();
                        emit backupFinished(false, tr("Unable to save backup"));
                    }
                    tarProcess->deleteLater();
                });

        tarProcess->start("tar", tarArgs);
    };

    auto runMessagesStep = [=]()
    {
        if (hasMessages)
        {
            QDir().mkpath(workDir + "/messages");
            Spawner::execute("commhistory-tool", {"export", "-groups", workDir + "/messages/groups.dat"}, runTarStep);
        }
        else
        {
            runTarStep();
        }
    };

    auto runCallsStep = [=]()
    {
        if (hasCalls)
        {
            QDir().mkpath(workDir + "/calls");
            Spawner::execute("commhistory-tool", {"export", "-calls", workDir + "/calls/calls.dat"}, runMessagesStep);
        }
        else
        {
            runMessagesStep();
        }
    };

    runCallsStep();
}

void Talteen::analyzeArchive(const QString &backupFile)
{
    QProcess *tarProcess = new QProcess(this);
    QByteArray *yamlData = new QByteArray();

    tarProcess->start("tar", {"-xzOf", backupFile, "content.yaml"});

    connect(tarProcess, &QProcess::readyReadStandardOutput, [=]()
            {
        yamlData->append(tarProcess->readAllStandardOutput());
        
        if (yamlData->contains("EOF: true")) {
            qDebug() << "content.yaml read. Stopping tar...";
            tarProcess->kill(); 
        } });

    connect(tarProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            [=](int, QProcess::ExitStatus)
            {
                if (!yamlData->contains("EOF: true"))
                {
                    emit archiveAnalyzed(false, tr("Unable to open backup"), QVariantMap());
                    tarProcess->deleteLater();
                    delete yamlData;
                    return;
                }

                QVariantMap metadata;
                QTextStream in(*yamlData);
                while (!in.atEnd())
                {
                    QString line = in.readLine();
                    QStringList parts = line.split(": ");
                    if (parts.size() == 2)
                    {
                        QString key = parts[0].trimmed();
                        QString value = parts[1].trimmed().remove("\"");
                        metadata.insert(key, value);
                    }
                }

                QString version = metadata.value("version").toString();
                if (version != "1.0.0")
                {
                    emit archiveAnalyzed(false, tr("Unsupported backup version"), QVariantMap());
                }
                else
                {
                    emit archiveAnalyzed(true, tr("Backup verified"), metadata);
                }

                tarProcess->deleteLater();
                delete yamlData;
            });
}

void Talteen::executeRestore(const QString &backupFile, const QVariantMap &selectedOptions)
{
    QString homePath = QDir::homePath();

    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QString workDir = cachePath + "/restore_workdir"; // Ensure it exists

    QFileInfo archiveInfo(backupFile);
    qint64 requiredSpace = archiveInfo.size() * 1.5;

    // Ensure it exists
    QDir().mkpath(cachePath);

    // Create the storage info object using our cache path
    QStorageInfo cacheStorage(cachePath);

    if (cacheStorage.bytesAvailable() < (requiredSpace + 104857600))
    {
        emit restoreFinished(false, tr("Not enough storage space to restore"));
        return;
    }

    QDir(workDir).removeRecursively();
    QDir().mkpath(workDir);

    qDebug() << "Extracting archive for restoring...";

    QProcess *tarProcess = new QProcess(this);
    tarProcess->start("tar", {"-xzpf", backupFile, "-C", workDir});

    connect(tarProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            [=](int exitCode, QProcess::ExitStatus)
            {
                if (exitCode != 0)
                {
                    emit restoreFinished(false, tr("Restore failed. The backup may be damaged"));
                    tarProcess->deleteLater();
                    return;
                }
                tarProcess->deleteLater();

                auto finalCleanup = [=]()
                {
                    QDir(workDir).removeRecursively();
                    emit restoreFinished(true, tr("Backup restored successfully"));
                };

                auto restoreMessages = [=]()
                {
                    if (selectedOptions.value("messages").toBool() && QFile::exists(workDir + "/messages/groups.dat"))
                    {
                        Spawner::execute("commhistory-tool", {"import", "-groups", workDir + "/messages/groups.dat"}, finalCleanup);
                    }
                    else
                    {
                        finalCleanup();
                    }
                };

                auto restoreCalls = [=]()
                {
                    if (selectedOptions.value("calls").toBool() && QFile::exists(workDir + "/calls/calls.dat"))
                    {
                        Spawner::execute("commhistory-tool", {"import", "-calls", workDir + "/calls/calls.dat"}, restoreMessages);
                    }
                    else
                    {
                        restoreMessages();
                    }
                };

                auto restoreFiles = [=]()
                {
                    QProcess *copyProcess = new QProcess(this);
                    QStringList rsyncArgs;
                    rsyncArgs << "-a";

                    bool hasFilesToCopy = false;

                    if (selectedOptions.value("appdata").toBool() && QDir(workDir + "/appdata").exists())
                    {
                        rsyncArgs << workDir + "/appdata/.config" << workDir + "/appdata/.local" << homePath + "/";
                        hasFilesToCopy = true;
                    }
                    if (selectedOptions.value("apporder").toBool() && QDir(workDir + "/apporder").exists())
                    {
                        QDir().mkpath(homePath + "/.config/lipstick");
                        QFile::copy(workDir + "/apporder/applications.menu", homePath + "/.config/lipstick/applications.menu");
                    }

                    if (selectedOptions.value("pictures").toBool() && QDir(workDir + "/pictures").exists())
                    {
                        rsyncArgs << workDir + "/pictures/" << QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/";
                        hasFilesToCopy = true;
                    }
                    if (selectedOptions.value("documents").toBool() && QDir(workDir + "/documents").exists())
                    {
                        rsyncArgs << workDir + "/documents/" << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/";
                        hasFilesToCopy = true;
                    }
                    if (selectedOptions.value("downloads").toBool() && QDir(workDir + "/downloads").exists())
                    {
                        rsyncArgs << workDir + "/downloads/" << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/";
                        hasFilesToCopy = true;
                    }
                    if (selectedOptions.value("music").toBool() && QDir(workDir + "/music").exists())
                    {
                        rsyncArgs << workDir + "/music/" << QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/";
                        hasFilesToCopy = true;
                    }
                    if (selectedOptions.value("videos").toBool() && QDir(workDir + "/videos").exists())
                    {
                        rsyncArgs << workDir + "/videos/" << QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/";
                        hasFilesToCopy = true;
                    }

                    if (hasFilesToCopy)
                    {
                        connect(copyProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                                [=](int, QProcess::ExitStatus)
                                {
                                    copyProcess->deleteLater();
                                    restoreCalls();
                                });
                        copyProcess->start("rsync", rsyncArgs);
                    }
                    else
                    {
                        copyProcess->deleteLater();
                        restoreCalls();
                    }
                };

                restoreFiles();
            });
}

QVariantList Talteen::getBackupFiles()
{
    QVariantList list;

    QString cacheFile = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/labels.ini";
    QSettings settings(cacheFile, QSettings::IniFormat);

    QStringList paths = {QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)};
    QString sdCard = getSdCardPath();
    if (!sdCard.isEmpty())
    {
        paths << sdCard + "/harbour-talteen";
    }

    for (const QString &path : paths)
    {
        QDir dir(path);
        if (!dir.exists())
            continue;

        QFileInfoList files = dir.entryInfoList(QStringList() << "*.talteen", QDir::Files, QDir::Time);

        for (const QFileInfo &file : files)
        {
            QVariantMap map;
            QString fileName = file.fileName();
            map["name"] = fileName;
            map["path"] = file.absoluteFilePath();
            map["size"] = file.size();
            map["date"] = file.lastModified().toString("yyyy-MM-dd HH:mm");
            map["location"] = (path.contains(sdCard) && !sdCard.isEmpty()) ? "SD Card" : "Internal";

            QString label = fileName;

            // Check the cache
            if (settings.contains(fileName))
            {
                label = settings.value(fileName).toString();
            }
            // Fetch it if missing
            else
            {
                QProcess tar;
                tar.start("tar", {"-xzOf", file.absoluteFilePath(), "content.yaml"});
                tar.waitForFinished(1000);

                if (tar.exitStatus() == QProcess::NormalExit)
                {
                    QString yaml = tar.readAllStandardOutput();
                    int labelIdx = yaml.indexOf("label: \"");
                    if (labelIdx != -1)
                    {
                        int startIdx = labelIdx + 8;
                        int endIdx = yaml.indexOf("\"", startIdx);
                        if (endIdx != -1)
                        {
                            label = yaml.mid(startIdx, endIdx - startIdx);
                        }
                    }
                }

                // Save to the cache file immediately
                settings.setValue(fileName, label);
            }

            map["label"] = label;
            list.append(map);
        }
    }
    return list;
}

bool Talteen::deleteBackup(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName();

    if (QFile::remove(filePath))
    {
        // Keep the cache clean when files are deleted
        QString cacheFile = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/labels.ini";
        QSettings settings(cacheFile, QSettings::IniFormat);
        settings.remove(fileName);

        return true;
    }
    return false;
}