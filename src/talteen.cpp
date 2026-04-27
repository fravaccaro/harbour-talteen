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
#include <functional>
#include <QSharedPointer>
#include <PackageKit/Daemon>
#include <PackageKit/Transaction>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>

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
    QString baseFileName = "talteen_backup_" + dateTimeString;

    QDir(workDir).removeRecursively();
    QDir().mkpath(workDir);

    bool hasAppinstalled = options.value("appinstalled").toBool();
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

    QDir().mkpath(targetFolder);
    QStorageInfo storage(targetFolder);

    // Check final destination space
    if (storage.bytesAvailable() < (estimatedSize + 104857600))
    {
        qDebug() << "[ERROR] Not enough free space in destination.";
        emit backupFinished(false, tr("Not enough storage space to save the backup"));
        return;
    }

    // Because of streaming, we only need ~1.5x the size in cache (just for the encrypted file and raw links)
    QDir().mkpath(cachePath);
    QStorageInfo cacheStorage(cachePath);
    if (cacheStorage.bytesAvailable() < (estimatedSize * 1.5 + 104857600))
    {
        qDebug() << "[ERROR] Not enough free space in internal cache memory.";
        emit backupFinished(false, tr("Not enough internal storage space to prepare the backup"));
        return;
    }

    qDebug() << "Start preparing backup...";

    QString password = options.value("password").toString();
    if (password.isEmpty())
    {
        emit backupFinished(false, tr("A password is required to save a backup"));
        return;
    }

    if (hasApporder)
    {
        QDir().mkpath(workDir + "/apporder");
        QFile::copy(homePath + "/.config/lipstick/applications.menu", workDir + "/apporder/applications.menu");
    }
    if (hasAppdata)
    {
        QDir().mkpath(workDir + "/appdata");
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

    // Outer Wrapper
    auto runOuterTarStep = [=]()
    {
        emit progressUpdate(tr("Finishing up..."));
        qDebug() << "Executing Step 4/4: Packing final archive...";

        QString backupFolder = (destOption == "internal" || destOption.isEmpty()) ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : destOption + "/harbour-talteen";
        QDir().mkpath(backupFolder);
        QString finalDestination = backupFolder + "/" + baseFileName + ".talteen";

        QProcess *outerTar = new QProcess(this);
        outerTar->setWorkingDirectory(workDir);
        outerTar->setProcessChannelMode(QProcess::ForwardedErrorChannel);

        outerTar->start("tar", {"-cf", finalDestination, "manifest.yaml", "payload.enc"});

        connect(outerTar, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus)
                {
                    QDir(workDir).removeRecursively();
                    if (exitCode == 0)
                    {
                        qDebug() << "Backup successfully saved in:" << finalDestination;
                        emit backupFinished(true, tr("Backup saved successfully"));
                    }
                    else
                    {
                        emit backupFinished(false, tr("Unable to save backup"));
                    }
                    outerTar->deleteLater();
                });
    };

    // Write YAML & Checksum
    auto writeYamlStep = [=](const QString &checksum)
    {
        emit progressUpdate(tr("Saving backup information..."));
        qDebug() << "Executing Step 3/4: Writing metadata and checksum...";
        QFile yamlFile(workDir + "/manifest.yaml");
        if (yamlFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream out(&yamlFile);
            out << "version: \"1.0.0\"\n";
            out << "time: \"" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\"\n";
            QString userLabel = options.value("label").toString().trimmed().replace("\"", "'");
            out << "label: \"" << (userLabel.isEmpty() ? baseFileName : userLabel) << "\"\n";
            out << "encrypted: true\n";
            out << "checksum: \"" << checksum << "\"\n";

            QStringList allCategories = {"appinstalled", "appdata", "apporder", "calls", "messages", "pictures", "documents", "downloads", "music", "videos"};
            for (const QString &category : allCategories)
            {
                out << category << ": " << (options.value(category).toBool() ? "true" : "false") << "\n";
            }
            out << "EOF: true\n";
            yamlFile.close();

            runOuterTarStep();
        }
        else
        {
            emit backupFinished(false, tr("Failed to write metadata file"));
        }
    };

    // Calculate Checksum
    auto runChecksumStep = [=]()
    {
        emit progressUpdate(tr("Creating integrity check..."));
        qDebug() << "Executing Step 2/4: Generating SHA-256 checksum...";
        QProcess *hashProc = new QProcess(this);
        hashProc->setWorkingDirectory(workDir);

        connect(hashProc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus)
                {
                    if (exitCode == 0)
                    {
                        QString output = QString(hashProc->readAllStandardOutput()).trimmed();
                        QString checksum = output.section(' ', 0, 0);
                        hashProc->deleteLater();
                        writeYamlStep(checksum);
                    }
                    else
                    {
                        emit backupFinished(false, tr("Failed to generate checksum"));
                        hashProc->deleteLater();
                    }
                });
        hashProc->start("sha256sum", {"payload.enc"});
    };

    // Stream Tar -> XZ -> OpenSSL
    auto runStreamingTarStep = [=]()
    {
        emit progressUpdate(tr("Creating secure backup..."));
        qDebug() << "Executing Step 1/4: Compressing (XZ) and encrypting payload stream...";

        QProcess *tarProcess = new QProcess(this);
        QProcess *sslProcess = new QProcess(this);

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("OPENSSL_CONF", "/dev/null");
        sslProcess->setProcessEnvironment(env);

        QProcessEnvironment tarEnv = QProcessEnvironment::systemEnvironment();
        tarEnv.insert("XZ_OPT", "-1");
        tarProcess->setProcessEnvironment(tarEnv);

        tarProcess->setWorkingDirectory(workDir);
        sslProcess->setWorkingDirectory(workDir);

        // Capture tar errors
        connect(tarProcess, &QProcess::readyReadStandardError, [=]()
                {
            QByteArray errorOutput = tarProcess->readAllStandardError();
            if (!errorOutput.trimmed().isEmpty()) {
                qDebug() << "[TAR LOG]" << errorOutput.trimmed();
            } });

        // Capture SSL errors
        connect(sslProcess, &QProcess::readyReadStandardError, [=]()
                {
            QByteArray errorOutput = sslProcess->readAllStandardError();
            if (!errorOutput.trimmed().isEmpty()) {
                qDebug() << "[SSL LOG]" << errorOutput.trimmed();
            } });
        // Connect the pipe: tar stdout feeds directly into openssl stdin
        tarProcess->setStandardOutputProcess(sslProcess);

        QStringList tarArgs;
        tarArgs << "-cJhf" << "-";

        if (hasAppinstalled)
            tarArgs << "appinstalled";
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

        QStringList sslArgs;
        sslArgs << "enc" << "-aes-256-cbc" << "-salt" << "-pbkdf2"
                << "-out" << "payload.enc" << "-pass" << "pass:" + password;

        connect(sslProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus)
                {
                    // Ensure tar also finished successfully
                    tarProcess->waitForFinished(2000);

                    if (exitCode == 0 && tarProcess->exitCode() == 0 && tarProcess->exitStatus() == QProcess::NormalExit)
                    {
                        runChecksumStep();
                    }
                    else
                    {
                        qDebug() << "[FATAL] Pipeline broken! OpenSSL exit:" << exitCode << "Tar exit:" << tarProcess->exitCode();
                        QFile::remove(workDir + "/payload.enc");
                        emit backupFinished(false, tr("Encryption or compression failed. Backup cancelled"));
                    }

                    tarProcess->deleteLater();
                    sslProcess->deleteLater();
                });

        connect(sslProcess, &QProcess::errorOccurred, [=](QProcess::ProcessError error)
                {
            qDebug() << "[FATAL] OpenSSL process error:" << error << sslProcess->errorString();
            emit backupFinished(false, tr("Unable to secure your backup. Is OpenSSL installed?"));
            tarProcess->kill();
            tarProcess->deleteLater();
            sslProcess->deleteLater(); });

        sslProcess->start("openssl", sslArgs);
        tarProcess->start("tar", tarArgs);
    };

    // Defined first so Calls knows about it
    auto runMessagesStep = [=]()
    {
        if (hasMessages)
        {
            emit progressUpdate(tr("Saving messages..."));
            qDebug() << "Exporting messages database...";
            QDir().mkpath(workDir + "/messages");
            Spawner::execute("commhistory-tool", {"export", "-groups", workDir + "/messages/groups.dat"}, runStreamingTarStep);
        }
        else
        {
            runStreamingTarStep();
        }
    };

    auto runCallsStep = [=]()
    {
        if (hasCalls)
        {
            emit progressUpdate(tr("Saving call history..."));
            qDebug() << "Exporting calls database...";
            QDir().mkpath(workDir + "/calls");
            Spawner::execute("commhistory-tool", {"export", "-calls", workDir + "/calls/calls.dat"}, runMessagesStep);
        }
        else
        {
            runMessagesStep();
        }
    };

    // Defined last because it is the first one to run
    auto runRsyncAppdataStep = [=]()
    {
        if (!hasAppdata)
        {
            runCallsStep();
            return;
        }

        emit progressUpdate(tr("Saving app data..."));
        qDebug() << "Executing Step 0: Syncing App Data to static snapshot...";
        QProcess *rsyncProcess = new QProcess(this);
        rsyncProcess->setProcessChannelMode(QProcess::ForwardedErrorChannel);

        QStringList rsyncArgs;
        rsyncArgs << "-a" << "--no-specials" << "--delete";

        QStringList excludePaths = {
            ".local/share/harbour-talteen",
            ".mozilla/lock", ".mozilla/.parentlock",
            ".local/share/org.sailfishos/browser/.mozilla/cache2",
            ".local/share/org.sailfishos/browser/.mozilla/startupCache",
            ".local/share/org.sailfishos/browser/.mozilla/OfflineCache",
            ".local/share/org.sailfishos/browser/.mozilla/safebrowsing",
            ".local/share/org.sailfishos/browser/.mozilla/minidumps",
            ".local/share/org.sailfishos/browser/.mozilla/crashes",
            ".local/share/org.sailfishos/browser/.mozilla/storage/temporary",
            ".config/Jolla", ".config/QtProject", ".config/dconf",
            ".config/libaccounts-glib", ".config/lipstick", ".config/nemo",
            ".config/nemomobile", ".config/pulse", ".config/signond",
            ".config/systemd", ".config/tracker", ".config/user-dirs.dirs",
            ".config/user-dirs.locale", ".config/.sailfish-gallery-reindex",
            ".local/nemo-transferengine", ".local/share/ambienced", ".local/share/applications",
            ".local/share/commhistory", ".local/share/dbus-1", ".local/share/gsettings-data-convert",
            ".local/share/maliit-server", ".local/share/system",
            ".local/share/systemd", ".local/share/telepathy", ".local/share/system/privilege/Contacts",
            ".local/share/tracker", ".local/share/xt9"};

        for (const QString &path : excludePaths)
        {
            rsyncArgs << "--exclude=" + path;
        }

        rsyncArgs << homePath + "/.config" << homePath + "/.local" << workDir + "/appdata/";

        connect(rsyncProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus exitStatus)
                {
                    if (exitStatus == QProcess::NormalExit && (exitCode == 0 || exitCode == 24))
                    {
                        runCallsStep(); // Move to the next step safely
                    }
                    else
                    {
                        qDebug() << "[FATAL] Rsync failed with exit code:" << exitCode;
                        emit backupFinished(false, tr("Unable to save app data"));
                    }
                    rsyncProcess->deleteLater();
                });

        rsyncProcess->start("rsync", rsyncArgs);
    };

    // Defined last because it will be the very first one to run
    auto runAppinstalledStep = [=]()
    {
        if (!hasAppinstalled)
        {
            runRsyncAppdataStep(); // Skip and move to the next step
            return;
        }

        emit progressUpdate(tr("Saving apps and repositories..."));
        qDebug() << "Executing Step 0: Exporting apps and repositories...";
        QDir().mkpath(workDir + "/appinstalled");

        // Repositories
        QProcess *repoProc = new QProcess(this);
        connect(repoProc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int, QProcess::ExitStatus)
                {
                    QFile repoFile(workDir + "/appinstalled/repositories.txt");
                    if (repoFile.open(QIODevice::WriteOnly | QIODevice::Text))
                    {
                        repoFile.write(repoProc->readAllStandardOutput());
                        repoFile.close();
                    }
                    repoProc->deleteLater();

                    // Installed Apps (Runs only after Repos finish)
                    QProcess *appProc = new QProcess(this);
                    connect(appProc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                            [=](int, QProcess::ExitStatus)
                            {
                                QFile appFile(workDir + "/appinstalled/appinstalled.txt");
                                if (appFile.open(QIODevice::WriteOnly | QIODevice::Text))
                                {
                                    appFile.write(appProc->readAllStandardOutput());
                                    appFile.close();
                                }
                                appProc->deleteLater();

                                // Move to the next step in the backup chain
                                runRsyncAppdataStep();
                            });

                    // C++ Strings don't need escaping for $ signs, so we can pass your awk exactly as is.
                    appProc->start("sh", {"-c", "pkcon get-packages --filter installed | awk '{print $2}' | grep -iE '^(harbour|openrepos|sailfishos|patchmanager)' | sed 's/-[0-9].*//'"});
                });

        repoProc->start("sh", {"-c", "ssu lr | grep -iE 'openrepos|chum' | awk '{ print $2, $4 }'"});
    };

    // Start the chain
    runAppinstalledStep();
}

void Talteen::analyzeArchive(const QString &backupFile)
{
    QProcess *tarProcess = new QProcess(this);
    QByteArray *yamlData = new QByteArray();

    tarProcess->start("tar", {"-xOf", backupFile, "manifest.yaml"});

    connect(tarProcess, &QProcess::readyReadStandardOutput, [=]()
            {
        yamlData->append(tarProcess->readAllStandardOutput());
        if (yamlData->contains("EOF: true")) {
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
                        metadata.insert(parts[0].trimmed(), parts[1].trimmed().remove("\""));
                    }
                }

                if (metadata.value("version").toString() != "1.0.0")
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
    QString workDir = cachePath + "/restore_workdir";

    QFileInfo archiveInfo(backupFile);

    // Only need 2x space for restore (encrypted payload + extracted raw files)
    qint64 requiredSpace = archiveInfo.size() * 2.5;

    QDir().mkpath(cachePath);
    QStorageInfo cacheStorage(cachePath);

    if (cacheStorage.bytesAvailable() < (requiredSpace + 104857600))
    {
        emit restoreFinished(false, tr("Not enough storage space to restore"));
        return;
    }

    QDir(workDir).removeRecursively();
    QDir().mkpath(workDir);

    auto finalCleanup = [=]()
    {
        QDir(workDir).removeRecursively();
        emit restoreFinished(true, tr("Backup restored successfully"));
    };

    auto restoreApps = [=]()
    {
        if (selectedOptions.value("appinstalled").toBool() && QDir(workDir + "/appinstalled").exists())
        {
            emit progressUpdate(tr("Restoring apps and repositories (this may take a while)..."));
            qDebug() << "Restoring repositories and applications...";

            // Add repositories via D-Bus
            QProcess ssuProc;
            ssuProc.start("ssu", {"lr"});
            ssuProc.waitForFinished();
            QString currentRepos = QString(ssuProc.readAllStandardOutput());

            QFile repoFile(workDir + "/appinstalled/repositories.txt");
            if (repoFile.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                // Connect to the SSU System Daemon
                QDBusInterface ssuInterface(
                    "org.nemo.ssu",
                    "/org/nemo/ssu",
                    "org.nemo.ssu",
                    QDBusConnection::systemBus());

                QTextStream in(&repoFile);
                while (!in.atEnd())
                {
                    QString line = in.readLine().trimmed();
                    if (!line.isEmpty())
                    {
                        QStringList parts = line.split(" ", QString::SkipEmptyParts);
                        if (parts.size() >= 2)
                        {
                            QString repoAlias = parts[0];
                            QString repoUrl = parts[1];

                            // Check if the system already has this repo
                            if (currentRepos.contains(repoAlias))
                            {
                                qDebug() << "Repository already enabled, skipping:" << repoAlias;
                                continue;
                            }

                            // Trigger the D-Bus addRepo method synchronously
                            QDBusReply<void> reply = ssuInterface.call("addRepo", repoAlias, repoUrl);
                            if (!reply.isValid())
                            {
                                qDebug() << "D-Bus SSU Error for" << repoAlias << ":" << reply.error().message();
                            }
                            else
                            {
                                qDebug() << "Successfully added repo via D-Bus:" << repoAlias;
                            }
                        }
                    }
                }
                repoFile.close();
            }

            // Read installed packages from backup
            QFile appFile(workDir + "/appinstalled/appinstalled.txt");
            QStringList packageNames;
            if (appFile.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                QTextStream in(&appFile);
                while (!in.atEnd())
                {
                    QString pkg = in.readLine().trimmed();
                    if (!pkg.isEmpty() && !packageNames.contains(pkg))
                    {
                        packageNames.append(pkg);
                    }
                }
                appFile.close();
            }

            qDebug() << "Apps found in backup file:" << packageNames.size();
            // If you want to see exactly what is in the file, uncomment the next line:
            // qDebug() << "Backup list:" << packageNames;

            if (packageNames.isEmpty())
            {
                qDebug() << "[WARNING] The appinstalled.txt file in this backup is empty!";
                finalCleanup();
                return;
            }

            // Exclude packages that are already installed on this device
            QProcess rpmProc;
            rpmProc.start("sh", {"-c", "rpm -qa --qf '%{NAME}\n'"});
            rpmProc.waitForFinished();

            if (rpmProc.exitStatus() == QProcess::NormalExit)
            {
                QStringList currentApps = QString(rpmProc.readAllStandardOutput()).split('\n', QString::SkipEmptyParts);
                qDebug() << "Apps currently installed on phone:" << currentApps.size();

                for (const QString &app : currentApps)
                {
                    packageNames.removeAll(app.trimmed());
                }
            }
            else
            {
                qDebug() << "[ERROR] Failed to run rpm command!";
            }

            qDebug() << "Apps missing and ready to be installed:" << packageNames.size();
            qDebug() << "Missing list:" << packageNames;

            if (packageNames.isEmpty())
            {
                qDebug() << "No missing packages found. Everything is already installed!";
                finalCleanup();
                return;
            }

            // Natively refresh PackageKit cache
            qDebug() << "Refreshing PackageKit cache natively...";

            auto refreshTrans = PackageKit::Daemon::refreshCache(true);

            // Handle the case where the user cancels or the system blocks the refresh
            connect(refreshTrans, &PackageKit::Transaction::errorCode, this, [=](PackageKit::Transaction::Error error, const QString &details)
                    {
                        qDebug() << "[PackageKit Refresh Error]" << error << "-" << details;
                        // We continue anyway, because a stale cache is better than no restore at all.
                    });

            connect(refreshTrans, &PackageKit::Transaction::finished, this, [=](PackageKit::Transaction::Exit, uint)
                    {
                        qDebug() << "Cache refresh attempt finished.";

                        // One-by-one search to prevent Zypper batch crashes
                        QSharedPointer<QStringList> packageIds(new QStringList());
                        QSharedPointer<QStringList> remainingNames(new QStringList(packageNames));

                        // Use QSharedPointer so the lambda can safely capture itself
                        QSharedPointer<std::function<void()>> resolveNext(new std::function<void()>());

                        *resolveNext = [=]()
                        {
                            if (remainingNames->isEmpty())
                            {
                                if (packageIds->isEmpty())
                                {
                                    qDebug() << "[ERROR] No valid packages found.";
                                    finalCleanup();
                                    return;
                                }

                                // Install the valid packages!
                                auto installTrans = PackageKit::Daemon::installPackages(*packageIds);

                                connect(installTrans, &PackageKit::Transaction::errorCode, this, [=](PackageKit::Transaction::Error error, const QString &details)
                                        { qDebug() << "[PackageKit Install Error]" << error << "-" << details; });

                                connect(installTrans, &PackageKit::Transaction::finished, this, [=](PackageKit::Transaction::Exit exitStatus, uint)
                                        {
                                            if (exitStatus == PackageKit::Transaction::ExitSuccess) {
                                                qDebug() << "Apps restored successfully.";
                                            } else {
                                                qDebug() << "App restore failed.";
                                            }
                                            finalCleanup(); });
                                return;
                            }

                            // Process the next package
                            QString currentPkg = remainingNames->takeFirst();
                            const PackageKit::Transaction::Filters searchFilters =
                                PackageKit::Transaction::FilterNewest | PackageKit::Transaction::FilterNotInstalled;
                            auto searchTrans = PackageKit::Daemon::searchNames(currentPkg, searchFilters);

                            connect(searchTrans, &PackageKit::Transaction::package, this, [packageIds, currentPkg](PackageKit::Transaction::Info, const QString &packageID, const QString &)
                                    {
                                        if (packageID.section(QLatin1Char(';'), 0, 0) != currentPkg)
                                            return;
                                        if (packageIds->contains(packageID))
                                            return;
                                        const QString pkgName = packageID.section(QLatin1Char(';'), 0, 0);
                                        for (const QString &existing : *packageIds)
                                        {
                                            if (existing.section(QLatin1Char(';'), 0, 0) == pkgName)
                                                return;
                                        }
                                        packageIds->append(packageID); });

                            connect(searchTrans, &PackageKit::Transaction::finished, this, [=]()
                                    {
                                        (*resolveNext)(); // Call the next iteration
                                    });
                        };

                        (*resolveNext)(); // Start the loop
                    });
        }
        else
        {
            finalCleanup();
        }
    };

    auto restoreMessages = [=]()
    {
        if (selectedOptions.value("messages").toBool() && QFile::exists(workDir + "/messages/groups.dat"))
        {
            emit progressUpdate(tr("Restoring messages..."));
            qDebug() << "Importing messages...";
            Spawner::execute("commhistory-tool", {"import", "-groups", workDir + "/messages/groups.dat"}, restoreApps);
        }
        else
        {
            restoreApps();
        }
    };

    auto restoreCalls = [=]()
    {
        if (selectedOptions.value("calls").toBool() && QFile::exists(workDir + "/calls/calls.dat"))
        {
            emit progressUpdate(tr("Restoring call history..."));
            qDebug() << "Importing calls...";
            Spawner::execute("commhistory-tool", {"import", "-calls", workDir + "/calls/calls.dat"}, restoreMessages);
        }
        else
        {
            restoreMessages();
        }
    };

    auto restoreFiles = [=]()
    {
        qDebug() << "Moving extracted files to their final destinations...";

        // Handle apporder immediately
        if (selectedOptions.value("apporder").toBool() && QDir(workDir + "/apporder").exists())
        {
            emit progressUpdate(tr("Restoring app grid layout..."));
            QDir().mkpath(homePath + "/.config/lipstick");
            QString destMenu = homePath + "/.config/lipstick/applications.menu";
            QFile::remove(destMenu);
            QFile::copy(workDir + "/apporder/applications.menu", destMenu);
        }

        // Use standard Qt StringLists to completely avoid the "incomplete struct" C++ compilation error
        QSharedPointer<QStringList> syncCmds(new QStringList());
        QSharedPointer<QStringList> syncMsgs(new QStringList());

        if (selectedOptions.value("appdata").toBool() && QDir(workDir + "/appdata").exists())
        {
            syncCmds->append(QString("rsync -a \"%1/appdata/.config\" \"%2/\" && rsync -a \"%1/appdata/.local\" \"%2/\"").arg(workDir, homePath));
            syncMsgs->append(tr("Restoring app data..."));
        }
        if (selectedOptions.value("pictures").toBool() && QDir(workDir + "/pictures").exists())
        {
            syncCmds->append(QString("rsync -a \"%1/pictures/\" \"%2/\"").arg(workDir, QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)));
            syncMsgs->append(tr("Restoring pictures..."));
        }
        if (selectedOptions.value("documents").toBool() && QDir(workDir + "/documents").exists())
        {
            syncCmds->append(QString("rsync -a \"%1/documents/\" \"%2/\"").arg(workDir, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)));
            syncMsgs->append(tr("Restoring documents..."));
        }
        if (selectedOptions.value("downloads").toBool() && QDir(workDir + "/downloads").exists())
        {
            syncCmds->append(QString("rsync -a \"%1/downloads/\" \"%2/\"").arg(workDir, QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)));
            syncMsgs->append(tr("Restoring downloads..."));
        }
        if (selectedOptions.value("music").toBool() && QDir(workDir + "/music").exists())
        {
            syncCmds->append(QString("rsync -a \"%1/music/\" \"%2/\"").arg(workDir, QStandardPaths::writableLocation(QStandardPaths::MusicLocation)));
            syncMsgs->append(tr("Restoring music..."));
        }
        if (selectedOptions.value("videos").toBool() && QDir(workDir + "/videos").exists())
        {
            syncCmds->append(QString("rsync -a \"%1/videos/\" \"%2/\"").arg(workDir, QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)));
            syncMsgs->append(tr("Restoring videos..."));
        }

        // Execute the commands one by one
        QSharedPointer<std::function<void()>> runNextTask(new std::function<void()>());
        *runNextTask = [=]()
        {
            if (syncCmds->isEmpty())
            {
                restoreCalls(); // Queue finished, move to next step
                return;
            }

            QString cmd = syncCmds->takeFirst();
            QString msg = syncMsgs->takeFirst();
            emit progressUpdate(msg);

            QProcess *copyProcess = new QProcess(this);
            connect(copyProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                    [=](int, QProcess::ExitStatus)
                    {
                        copyProcess->deleteLater();
                        (*runNextTask)();
                    });
            copyProcess->start("sh", {"-c", cmd});
        };

        (*runNextTask)();
    };

    auto runStreamingExtractStep = [=]()
    {
        emit progressUpdate(tr("Preparing files for restore..."));
        qDebug() << "Executing Step 4/4: Decrypting and extracting (XZ) on the fly...";
        QString password = selectedOptions.value("password").toString();

        QProcess *sslProcess = new QProcess(this);
        QProcess *tarProcess = new QProcess(this);

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("OPENSSL_CONF", "/dev/null");
        sslProcess->setProcessEnvironment(env);

        sslProcess->setWorkingDirectory(workDir);
        tarProcess->setWorkingDirectory(workDir);

        sslProcess->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        tarProcess->setProcessChannelMode(QProcess::ForwardedErrorChannel);

        sslProcess->setStandardOutputProcess(tarProcess);

        QStringList sslArgs;
        sslArgs << "enc" << "-d" << "-aes-256-cbc" << "-pbkdf2"
                << "-in" << "payload.enc"
                << "-pass" << "pass:" + password;

        QStringList tarArgs;
        // -x reads, -J uses xz, -p preserves permissions, -f - reads from standard input
        tarArgs << "-xJpf" << "-" << "-C" << workDir;

        // We bind to the end of the pipeline (tar) to know when we are completely done
        connect(tarProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus)
                {
                    sslProcess->waitForFinished(2000); // Give SSL a moment to close properly

                    if (exitCode == 0 && sslProcess->exitCode() == 0)
                    {
                        QFile::remove(workDir + "/payload.enc");
                        restoreFiles();
                    }
                    else
                    {
                        qDebug() << "[FATAL] Restore pipeline broken! Tar exit:" << exitCode << "OpenSSL exit:" << sslProcess->exitCode();
                        emit restoreFinished(false, tr("Unable to unlock the backup. Please check your password"));
                    }

                    sslProcess->deleteLater();
                    tarProcess->deleteLater();
                });

        connect(sslProcess, &QProcess::errorOccurred, [=](QProcess::ProcessError error)
                {
            qDebug() << "[FATAL] OpenSSL decrypt process error:" << error << sslProcess->errorString();
            emit restoreFinished(false, tr("Encryption tool failed to start. Is OpenSSL installed?"));
            tarProcess->kill();
            sslProcess->deleteLater();
            tarProcess->deleteLater(); });

        tarProcess->start("tar", tarArgs);
        sslProcess->start("openssl", sslArgs);
    };

    // Verify Checksum
    auto runVerifyChecksumStep = [=](const QString &expectedChecksum)
    {
        emit progressUpdate(tr("Verifying backup integrity..."));
        qDebug() << "Executing Step 3/4: Verifying SHA-256 checksum...";
        QProcess *hashProc = new QProcess(this);
        hashProc->setWorkingDirectory(workDir);

        connect(hashProc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus)
                {
                    if (exitCode == 0)
                    {
                        QString output = QString(hashProc->readAllStandardOutput()).trimmed();
                        QString actualChecksum = output.section(' ', 0, 0);
                        hashProc->deleteLater();

                        if (actualChecksum == expectedChecksum)
                        {
                            qDebug() << "Checksum verified successfully!";
                            runStreamingExtractStep();
                        }
                        else
                        {
                            emit restoreFinished(false, tr("Backup may be damaged (Checksum mismatch)"));
                        }
                    }
                    else
                    {
                        emit restoreFinished(false, tr("Failed to verify checksum"));
                        hashProc->deleteLater();
                    }
                });
        hashProc->start("sha256sum", {"payload.enc"});
    };

    // Read YAML metadata
    auto runReadYamlStep = [=]()
    {
        emit progressUpdate(tr("Loading backup details..."));
        qDebug() << "Executing Step 2/4: Reading metadata for checksum...";
        QFile yamlFile(workDir + "/manifest.yaml");
        if (yamlFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&yamlFile);
            QString expectedChecksum = "";

            while (!in.atEnd())
            {
                QString line = in.readLine();
                if (line.startsWith("checksum: "))
                {
                    expectedChecksum = line.split(": ")[1].trimmed().remove("\"");
                    break;
                }
            }
            yamlFile.close();

            if (expectedChecksum.isEmpty())
            {
                qDebug() << "[FATAL] No checksum found in archive metadata.";
                emit restoreFinished(false, tr("Invalid backup format. Checksum is missing"));
                return;
            }

            runVerifyChecksumStep(expectedChecksum);
        }
        else
        {
            emit restoreFinished(false, tr("Failed to read backup metadata"));
        }
    };

    // Outer Extract
    emit progressUpdate(tr("Opening backup..."));
    qDebug() << "Executing Step 1/4: Extracting outer wrapper...";

    QString password = selectedOptions.value("password").toString();
    if (password.isEmpty())
    {
        emit restoreFinished(false, tr("Password required for this backup"));
        return;
    }

    QProcess *outerTarProcess = new QProcess(this);
    outerTarProcess->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    outerTarProcess->start("tar", {"-xf", backupFile, "-C", workDir});

    connect(outerTarProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            [=](int exitCode, QProcess::ExitStatus)
            {
                outerTarProcess->deleteLater();
                if (exitCode == 0)
                {
                    runReadYamlStep();
                }
                else
                {
                    emit restoreFinished(false, tr("Restore failed. The backup may be damaged"));
                }
            });
}

QVariantList
Talteen::getBackupFiles()
{
    QVariantList list;

    QString cacheFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/labels.ini";
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

            if (settings.contains(fileName))
            {
                label = settings.value(fileName).toString();
            }
            else
            {
                QProcess tar;
                tar.start("tar", {"-xOf", file.absoluteFilePath(), "manifest.yaml"});
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
        QString cacheFile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/labels.ini";
        QSettings settings(cacheFile, QSettings::IniFormat);
        settings.remove(fileName);

        return true;
    }
    return false;
}