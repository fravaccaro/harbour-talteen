#include "talteen.h"
#include "spawner.h"
#include "talteen_crypto.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QProcess>
#include <QByteArray>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTextStream>
#include <QMap>
#include <QVector>

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

    // Sailfish OS 5.x ships BusyBox tar, which rejects GNU long options and aborts
    // before writing a single byte, so support has to be probed instead of assumed.
    bool tarSupportsIgnoreFailedRead()
    {
        static const bool supported = []() {
            QProcess probe;
            // BusyBox answers with its whole usage text; keep it out of the log.
            probe.setStandardOutputFile(QProcess::nullDevice());
            probe.setStandardErrorFile(QProcess::nullDevice());
            probe.start(QStringLiteral("tar"),
                        {QStringLiteral("--ignore-failed-read"), QStringLiteral("--version")});
            return probe.waitForFinished(5000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
        }();
        return supported;
    }

    // Archiving dereferences symlinks, so a single dangling one aborts tar. Recursion
    // deliberately stops at every symlink: descending through one would leave the
    // staging copy and start deleting from the user's real directories.
    int pruneBrokenSymlinks(const QString &dirPath)
    {
        int removed = 0;
        const QFileInfoList entries = QDir(dirPath).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);

        for (const QFileInfo &entry : entries)
        {
            if (entry.isSymLink())
            {
                if (!entry.exists() && QFile::remove(entry.absoluteFilePath()))
                    ++removed;
            }
            else if (entry.isDir())
            {
                removed += pruneBrokenSymlinks(entry.absoluteFilePath());
            }
        }
        return removed;
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
    QString dateTimeString = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm");
    QString baseFileName = "talteen_backup_" + dateTimeString;

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
    if (destOption.isEmpty())
    {
        emit backupFinished(false, tr("SD card not found"), QString(), 0, QString());
        return;
    }

    const bool useInternal = (destOption == QLatin1String("internal"));
    const QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString stagingBase = useInternal
                                    ? appDataLocation + QStringLiteral("/.staging")
                                    : destOption + QStringLiteral("/harbour-talteen/.staging");
    const QString workDir = stagingBase + QStringLiteral("/workdir");
    const QString backupFolder = useInternal ? appDataLocation : destOption + QStringLiteral("/harbour-talteen");
    const QString targetFolder = useInternal ? homePath : destOption;

    QDir(stagingBase).removeRecursively();
    QDir().mkpath(workDir);

    QDir().mkpath(targetFolder);
    QStorageInfo storage(stagingBase);

    if (storage.bytesAvailable() < (static_cast<qint64>(estimatedSize * 1.5) + 104857600))
    {
        qDebug() << "[ERROR] Not enough free space in destination.";
        emit backupFinished(false, tr("Not enough storage space to save the backup"), QString(), 0, QString());
        return;
    }

    qDebug() << "Start preparing backup...";

    QString password = options.value("password").toString();
    if (password.isEmpty())
    {
        emit backupFinished(false, tr("A password is required to save a backup"), QString(), 0, QString());
        return;
    }

    if (hasApporder)
    {
        QString lipstickPath = homePath + "/.config/lipstick";
        if (QDir(lipstickPath).exists())
        {
            QString apporderPath = workDir + "/apporder";
            QDir().mkpath(apporderPath);

            QString srcMenu = lipstickPath + "/applications.menu";
            QString dstMenu = apporderPath + "/applications.menu";
            if (QFile::exists(srcMenu))
                QFile::copy(srcMenu, dstMenu);

            QDir lipstickDir(lipstickPath);
            QStringList folderFiles = lipstickDir.entryList(QStringList() << "Folder*.directory", QDir::Files);
            for (const QString &fileName : folderFiles)
                QFile::copy(lipstickPath + "/" + fileName, apporderPath + "/" + fileName);
        }
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

    // Set when tar could not read part of the selection; the archive is still usable.
    QSharedPointer<bool> incompleteSelection(new bool(false));

    // Outer Wrapper
    auto runOuterTarStep = [=]()
    {
        emit progressUpdate(tr("Finishing up..."));
        qDebug() << "Executing Step 3/3: Outer archive pack...";

        QDir().mkpath(backupFolder);
        QString finalDestination = backupFolder + "/" + baseFileName + ".talteen";

        QProcess *outerTar = new QProcess(this);
        outerTar->setWorkingDirectory(workDir);
        outerTar->setProcessChannelMode(QProcess::ForwardedErrorChannel);

        outerTar->start("tar", {"-cf", finalDestination, "manifest.yaml", "payload.enc"});

        connect(outerTar, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus)
                {
                    QDir(stagingBase).removeRecursively();
                    if (exitCode == 0)
                    {
                        qDebug() << "Backup successfully saved in:" << finalDestination;
                        QFileInfo fi(finalDestination);
                        const QString successMessage = *incompleteSelection
                                                           ? tr("Backup saved. Some files could not be read and were skipped")
                                                           : tr("Backup saved successfully");
                        emit backupFinished(true, successMessage, finalDestination, fi.size(),
                                            fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
                    }
                    else
                    {
                        emit backupFinished(false, tr("Unable to save backup"), QString(), 0, QString());
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
            emit backupFinished(false, tr("Failed to write metadata file"), QString(), 0, QString());
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

        // Plaintext bytes received from tar, used to tell a partial archive apart
        // from one that was never started at all.
        QSharedPointer<qint64> streamedBytes(new qint64(0));

        QByteArray salt(16, 0);
        QByteArray iv(12, 0);
        if (!fillRandomBytes(&salt) || !fillRandomBytes(&iv))
        {
            emit backupFinished(false, tr("Unable to generate encryption parameters"), QString(), 0, QString());
            tarProcess->deleteLater();
            delete encFile;
            return;
        }

        const int iterations = 180000;
        QByteArray key;
        if (!deriveKeyPbkdf2(password, salt, iterations, &key))
        {
            emit backupFinished(false, tr("Unable to derive encryption key"), QString(), 0, QString());
            tarProcess->deleteLater();
            delete encFile;
            return;
        }

        if (!encFile->open(QIODevice::WriteOnly))
        {
            emit backupFinished(false, tr("Unable to write encrypted payload"), QString(), 0, QString());
            tarProcess->deleteLater();
            delete encFile;
            return;
        }

        QSharedPointer<QString> cryptoError(new QString());
        const QByteArray aad("talteen:v2");
        EVP_CIPHER_CTX *ctx = createAesGcmEncryptContext(key, iv, aad, cryptoError.data());
        if (!ctx)
        {
            emit backupFinished(false, *cryptoError, QString(), 0, QString());
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
            *streamedBytes += inChunk.size();

            QByteArray outChunk;
            if (!encryptAesGcmChunk(ctx, inChunk, &outChunk, cryptoError.data())) {
                *cryptoOk = false;
                return;
            }
            if (encFile->write(outChunk.constData(), outChunk.size()) != outChunk.size()) {
                *cryptoOk = false;
                return;
            } });

        const int prunedLinks = pruneBrokenSymlinks(workDir);
        if (prunedLinks > 0)
            qDebug() << "Dropped broken symlinks before packaging:" << prunedLinks;

        const QVector<QPair<bool, QString>> categories = {
            {hasAppinstalled, QStringLiteral("appinstalled")},
            {hasApporder, QStringLiteral("apporder")},
            {hasCalls, QStringLiteral("calls")},
            {hasMessages, QStringLiteral("messages")},
            {hasAppdata, QStringLiteral("appdata")},
            {hasPictures, QStringLiteral("pictures")},
            {hasDocuments, QStringLiteral("documents")},
            {hasDownloads, QStringLiteral("downloads")},
            {hasMusic, QStringLiteral("music")},
            {hasVideos, QStringLiteral("videos")}};

        QStringList stagedEntries;
        for (const QPair<bool, QString> &category : categories)
        {
            if (!category.first)
                continue;

            // Media categories are staged as symlinks to the user directories, which
            // may not exist. Naming one would abort the whole archive.
            if (!QFileInfo::exists(workDir + QLatin1Char('/') + category.second))
            {
                qDebug() << "[WARNING] Nothing staged for category, skipping:" << category.second;
                *incompleteSelection = true;
                continue;
            }
            stagedEntries << category.second;
        }

        if (stagedEntries.isEmpty())
        {
            qDebug() << "[FATAL] None of the selected categories produced any data.";
            emit backupFinished(false, tr("None of the selected content could be found"), QString(), 0, QString());
            freeCipherContext(ctx);
            encFile->close();
            delete encFile;
            tarProcess->deleteLater();
            return;
        }

        QStringList tarArgs;
        if (tarSupportsIgnoreFailedRead())
            tarArgs << "--ignore-failed-read";
        tarArgs << "-cJhf" << "-" << stagedEntries;

        connect(tarProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus tarExit)
                {
                    const QByteArray trailing = tarProcess->readAllStandardOutput();
                    if (*cryptoOk && !trailing.isEmpty())
                    {
                        *streamedBytes += trailing.size();
                        QByteArray outChunk;
                        if (!encryptAesGcmChunk(ctx, trailing, &outChunk, cryptoError.data()) || encFile->write(outChunk.constData(), outChunk.size()) != outChunk.size())
                        {
                            *cryptoOk = false;
                        }
                    }

                    // Exit 1 means individual entries were unreadable or vanished while
                    // being read; the archive itself is complete. GNU tar reserves 2 and
                    // above for fatal errors, BusyBox tar signals them with an empty stream.
                    const bool tarWarnedOnly = (exitCode == 1 && *streamedBytes > 0);
                    const bool tarOk = (tarExit == QProcess::NormalExit && (exitCode == 0 || tarWarnedOnly));
                    if (tarWarnedOnly)
                    {
                        qDebug() << "[WARNING] Tar skipped unreadable entries, archive kept.";
                        *incompleteSelection = true;
                    }

                    if (tarOk && *cryptoOk)
                    {
                        QByteArray finalChunk;
                        QByteArray tag;
                        if (!finalizeAesGcmEncrypt(ctx, &finalChunk, &tag, cryptoError.data()))
                        {
                            *cryptoOk = false;
                        }
                        else if (finalChunk.size() > 0 && encFile->write(finalChunk.constData(), finalChunk.size()) != finalChunk.size())
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
                        emit backupFinished(false, tr("Encryption or compression failed. Backup cancelled"), QString(), 0, QString());
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
            ".local/share/harbour-talteen/.staging",
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
                    // 23 is a partial transfer caused by unreadable files, 24 one caused
                    // by files vanishing mid-copy. Both leave a usable staging tree, and
                    // both are normal on a live device, so neither may abort the backup.
                    const bool rsyncPartial = (exitCode == 23 || exitCode == 24);
                    if (exitStatus == QProcess::NormalExit && (exitCode == 0 || rsyncPartial))
                    {
                        if (rsyncPartial)
                        {
                            qDebug() << "[WARNING] Rsync could not copy every file, exit:" << exitCode;
                            *incompleteSelection = true;
                        }
                        runCallsStep(); // Move to the next step safely
                    }
                    else
                    {
                        qDebug() << "[FATAL] Rsync failed with exit code:" << exitCode;
                        emit backupFinished(false, tr("Unable to save app data"), QString(), 0, QString());
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

        repoProc->start("sh", {"-c", "ssu lr 2>&1 | grep -iE 'openrepos|chum|harbour-' | grep -v ' - store ' | awk '/- / { alias=$2; url=\"\"; for(i=NF;i>=1;i--) if($i ~ /^https?:\\/\\//){url=$i; break} if(alias!=\"\" && url!=\"\") print alias, url }'"});
    };

    // Start the chain
    runAppinstalledStep();
}
