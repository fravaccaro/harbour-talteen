#include "talteen.h"
#include "spawner.h"
#include "talteen_crypto.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTextStream>
#include <QMap>

#include <PackageKit/Daemon>
#include <PackageKit/Transaction>

#include <functional>

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
            emit progressUpdate(tr("Restoring apps..."));
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
            qDebug() << "Refreshing PackageKit cache...";

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

    auto emitMetadataError = [=](const QString &fieldOrReason)
    {
        emit restoreFinished(false, tr("Invalid v2 backup metadata: %1").arg(fieldOrReason));
    };

    // Read YAML metadata
    auto runReadYamlStep = [=]()
    {
        emit progressUpdate(tr("Loading backup details..."));
        qDebug() << "Executing Step 2/4: Reading metadata...";
        QFile yamlFile(workDir + "/manifest.yaml");
        if (yamlFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&yamlFile);
            QMap<QString, QString> metadata;

            while (!in.atEnd())
            {
                const QString line = in.readLine();
                const int sep = line.indexOf(": ");
                if (sep > 0)
                {
                    const QString key = line.left(sep).trimmed();
                    const QString value = line.mid(sep + 2).trimmed().remove("\"");
                    metadata.insert(key, value);
                }
            }
            yamlFile.close();

            const QString version = metadata.value("version");
            if (version == "1.0.0")
            {
                const QString expectedChecksum = metadata.value("checksum");
                if (expectedChecksum.isEmpty())
                {
                    emit restoreFinished(false, tr("Invalid legacy backup. Checksum is missing"));
                    return;
                }
                qDebug() << "V1 Backup detected. Running legacy checksum...";
                runVerifyChecksumStep(expectedChecksum);
            }
            else if (version == "2.0.0")
            {
                if (metadata.value("encrypted") != "true")
                {
                    emitMetadataError(tr("encrypted must be true"));
                    return;
                }
                if (metadata.value("encryption") != "openssl-aes-256-gcm")
                {
                    emit restoreFinished(false, tr("Unsupported backup encryption format"));
                    return;
                }
                if (metadata.value("kdf") != "pbkdf2-hmac-sha256")
                {
                    emit restoreFinished(false, tr("Unsupported backup key derivation format"));
                    return;
                }

                bool ok = false;
                const int iterations = metadata.value("kdf_iterations").toInt(&ok);
                if (!ok || iterations < 50000 || iterations > 5000000)
                {
                    emitMetadataError(tr("invalid kdf_iterations"));
                    return;
                }

                const QString saltB64 = metadata.value("salt_b64");
                const QString ivB64 = metadata.value("iv_b64");
                const QString tagB64 = metadata.value("tag_b64");
                const QString aad = metadata.value("aad");
                if (saltB64.isEmpty())
                {
                    emitMetadataError(tr("missing salt_b64"));
                    return;
                }
                if (ivB64.isEmpty())
                {
                    emitMetadataError(tr("missing iv_b64"));
                    return;
                }
                if (tagB64.isEmpty())
                {
                    emitMetadataError(tr("missing tag_b64"));
                    return;
                }
                if (aad.isEmpty())
                {
                    emitMetadataError(tr("missing aad"));
                    return;
                }
                if (aad != "talteen:v2")
                {
                    emitMetadataError(tr("invalid aad"));
                    return;
                }

                const QByteArray salt = QByteArray::fromBase64(saltB64.toUtf8());
                const QByteArray iv = QByteArray::fromBase64(ivB64.toUtf8());
                const QByteArray tag = QByteArray::fromBase64(tagB64.toUtf8());
                if (salt.size() != 16)
                {
                    emitMetadataError(tr("salt_b64 has invalid length"));
                    return;
                }
                if (iv.size() != 12)
                {
                    emitMetadataError(tr("iv_b64 has invalid length"));
                    return;
                }
                if (tag.size() != 16)
                {
                    emitMetadataError(tr("tag_b64 has invalid length"));
                    return;
                }

                QByteArray key;
                const QString password = selectedOptions.value("password").toString();
                if (!deriveKeyPbkdf2(password, salt, iterations, &key))
                {
                    emit restoreFinished(false, tr("Unable to derive decryption key"));
                    return;
                }

                emit progressUpdate(tr("Decrypting backup payload..."));
                QString cryptoError;
                QFile encFile(workDir + "/payload.enc");
                if (!encFile.open(QIODevice::ReadOnly))
                {
                    emit restoreFinished(false, tr("Unable to read encrypted payload"));
                    return;
                }

                EVP_CIPHER_CTX *ctx = createAesGcmDecryptContext(key, iv, aad.toUtf8(), tag, &cryptoError);
                if (!ctx)
                {
                    emit restoreFinished(false, tr("Unable to initialize decryption"));
                    return;
                }

                emit progressUpdate(tr("Preparing files for restore..."));
                QProcess *tarProcess = new QProcess(this);
                tarProcess->setWorkingDirectory(workDir);
                tarProcess->setProcessChannelMode(QProcess::ForwardedErrorChannel);
                tarProcess->start("tar", {"-xJpf", "-", "-C", workDir});
                if (!tarProcess->waitForStarted(5000))
                {
                    freeCipherContext(ctx);
                    tarProcess->deleteLater();
                    emit restoreFinished(false, tr("Unable to unpack restored files"));
                    return;
                }

                bool streamOk = true;
                while (streamOk)
                {
                    const QByteArray inChunk = encFile.read(65536);
                    if (inChunk.isEmpty())
                        break;

                    QByteArray outChunk;
                    if (!decryptAesGcmChunk(ctx, inChunk, &outChunk, &cryptoError))
                    {
                        streamOk = false;
                        break;
                    }
                    if (!outChunk.isEmpty() && tarProcess->write(outChunk) != outChunk.size())
                    {
                        streamOk = false;
                        cryptoError = tr("Unable to stream decrypted payload");
                        break;
                    }
                }

                if (streamOk && encFile.error() != QFile::NoError)
                {
                    streamOk = false;
                    cryptoError = tr("Unable to read encrypted payload");
                }

                if (streamOk)
                {
                    QByteArray finalChunk;
                    if (!finalizeAesGcmDecrypt(ctx, &finalChunk, &cryptoError))
                        streamOk = false;
                    else if (!finalChunk.isEmpty() && tarProcess->write(finalChunk) != finalChunk.size())
                    {
                        streamOk = false;
                        cryptoError = tr("Unable to stream decrypted payload");
                    }
                }

                freeCipherContext(ctx);

                tarProcess->closeWriteChannel();
                connect(tarProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                        [=, streamOk, cryptoError](int exitCode, QProcess::ExitStatus status)
                        {
                            tarProcess->deleteLater();
                            QFile::remove(workDir + "/payload.enc");
                            if (!streamOk)
                            {
                                qDebug() << "[FATAL] OpenSSL GCM decrypt failed:" << cryptoError;
                                emit restoreFinished(false, tr("Unable to unlock backup (wrong password or modified backup)"));
                            }
                            else if (status == QProcess::NormalExit && exitCode == 0)
                            {
                                restoreFiles();
                            }
                            else
                            {
                                emit restoreFinished(false, tr("Unable to unpack restored files"));
                            }
                        });
            }
            else
            {
                emit restoreFinished(false, tr("Unsupported backup version"));
            }
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
