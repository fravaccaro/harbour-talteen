#include "talteen.h"
#include "spawner.h"
#include "talteen_crypto.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QByteArray>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTextStream>
#include <QMap>

namespace
{
    bool fillRandomBytes(QByteArray *buffer)
    {
        QFile rnd(QStringLiteral("/dev/urandom"));
        if (!rnd.open(QIODevice::ReadOnly))
            return false;
        const QByteArray data = rnd.read(buffer->size());
        if (data.size() != buffer->size())
            return false;
        *buffer = data;
        return true;
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
        qDebug() << "Executing Step 3/3: Outer archive pack...";

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

    // Write YAML metadata
    auto writeYamlStep = [=](const QMap<QString, QString> &extraMetadata)
    {
        emit progressUpdate(tr("Saving backup information..."));
        qDebug() << "Executing Step 2/3: Write manifest...";
        QFile yamlFile(workDir + "/manifest.yaml");
        if (yamlFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream out(&yamlFile);
            out << "version: \"2.0.0\"\n";
            out << "encryption: \"openssl-aes-256-gcm\"\n";
            out << "time: \"" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\"\n";
            QString userLabel = options.value("label").toString().trimmed().replace("\"", "'");
            out << "label: \"" << (userLabel.isEmpty() ? baseFileName : userLabel) << "\"\n";
            out << "encrypted: true\n";
            out << "kdf: \"pbkdf2-hmac-sha256\"\n";
            out << "aad: \"talteen:v2\"\n";

            for (auto it = extraMetadata.constBegin(); it != extraMetadata.constEnd(); ++it)
            {
                out << it.key() << ": \"" << it.value() << "\"\n";
            }

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

    // Stream tar.xz stdout directly into OpenSSL EVP AES-256-GCM -> payload.enc
    auto runStreamingTarStep = [=]()
    {
        emit progressUpdate(tr("Creating secure backup..."));
        qDebug() << "Executing Step 1/3: Stream tar+xz+gcm encryption...";

        QProcess *tarProcess = new QProcess(this);
        QFile *encFile = new QFile(workDir + "/payload.enc");

        // Standard tar environment optimization
        QProcessEnvironment tarEnv = QProcessEnvironment::systemEnvironment();
        tarEnv.insert("XZ_OPT", "-1");
        tarProcess->setProcessEnvironment(tarEnv);

        tarProcess->setWorkingDirectory(workDir);

        // Capture tar errors for debugging
        connect(tarProcess, &QProcess::readyReadStandardError, [=]()
                {
            QByteArray errorOutput = tarProcess->readAllStandardError();
            if (!errorOutput.trimmed().isEmpty()) {
                qDebug() << "[TAR LOG]" << errorOutput.trimmed();
            } });

        QByteArray salt(16, 0);
        QByteArray iv(12, 0);
        if (!fillRandomBytes(&salt) || !fillRandomBytes(&iv))
        {
            emit backupFinished(false, tr("Unable to generate encryption parameters"));
            tarProcess->deleteLater();
            delete encFile;
            return;
        }

        const int iterations = 180000;
        QByteArray key;
        if (!deriveKeyPbkdf2(password, salt, iterations, &key))
        {
            emit backupFinished(false, tr("Unable to derive encryption key"));
            tarProcess->deleteLater();
            delete encFile;
            return;
        }

        if (!encFile->open(QIODevice::WriteOnly))
        {
            emit backupFinished(false, tr("Unable to write encrypted payload"));
            tarProcess->deleteLater();
            delete encFile;
            return;
        }

        QSharedPointer<QString> cryptoError(new QString());
        const QByteArray aad("talteen:v2");
        EVP_CIPHER_CTX *ctx = createAesGcmEncryptContext(key, iv, aad, cryptoError.data());
        if (!ctx)
        {
            emit backupFinished(false, *cryptoError);
            tarProcess->deleteLater();
            encFile->close();
            delete encFile;
            return;
        }

        QSharedPointer<bool> cryptoOk(new bool(true));

        connect(tarProcess, &QProcess::readyReadStandardOutput, this, [=]()
                {
            if (!*cryptoOk) {
                tarProcess->readAllStandardOutput();
                return;
            }

            const QByteArray inChunk = tarProcess->readAllStandardOutput();
            if (inChunk.isEmpty()) {
                return;
            }

            QByteArray outChunk;
            if (!encryptAesGcmChunk(ctx, inChunk, &outChunk, cryptoError.data())) {
                *cryptoOk = false;
                return;
            }
            if (encFile->write(outChunk.constData(), outChunk.size()) != outChunk.size()) {
                *cryptoOk = false;
                return;
            } });

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

        connect(tarProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus tarExit)
                {
                    const QByteArray trailing = tarProcess->readAllStandardOutput();
                    if (*cryptoOk && !trailing.isEmpty())
                    {
                        QByteArray outChunk;
                        if (!encryptAesGcmChunk(ctx, trailing, &outChunk, cryptoError.data())
                            || encFile->write(outChunk.constData(), outChunk.size()) != outChunk.size())
                        {
                            *cryptoOk = false;
                        }
                    }

                    const bool tarOk = (tarExit == QProcess::NormalExit && exitCode == 0);
                    if (tarOk && *cryptoOk)
                    {
                        QByteArray finalChunk;
                        QByteArray tag;
                        if (!finalizeAesGcmEncrypt(ctx, &finalChunk, &tag, cryptoError.data()))
                        {
                            *cryptoOk = false;
                        }
                        else if (finalChunk.size() > 0
                                 && encFile->write(finalChunk.constData(), finalChunk.size()) != finalChunk.size())
                        {
                            *cryptoOk = false;
                        }
                        else
                        {
                            QMap<QString, QString> extra;
                            extra.insert("kdf_iterations", QString::number(iterations));
                            extra.insert("salt_b64", QString::fromLatin1(salt.toBase64()));
                            extra.insert("iv_b64", QString::fromLatin1(iv.toBase64()));
                            extra.insert("tag_b64", QString::fromLatin1(tag.toBase64()));
                            writeYamlStep(extra);
                        }
                    }

                    if (!tarOk || !*cryptoOk)
                    {
                        if (!tarOk)
                            qDebug() << "[FATAL] Tar packaging failed, exit:" << exitCode;
                        else
                            qDebug() << "[FATAL] OpenSSL GCM streaming encryption failed:" << *cryptoError;
                        QFile::remove(workDir + "/payload.enc");
                        emit backupFinished(false, tr("Encryption or compression failed. Backup cancelled"));
                    }

                    freeCipherContext(ctx);
                    encFile->close();
                    delete encFile;
                    tarProcess->deleteLater();
                });
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
        qDebug() << "Executing Step 0/3: Collect app metadata / rsync prep...";
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

        emit progressUpdate(tr("Saving installed apps..."));
        qDebug() << "Executing Step 0/3: Collect app metadata / rsync prep...";
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
