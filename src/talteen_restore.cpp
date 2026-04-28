#include "talteen.h"
#include "spawner.h"

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
