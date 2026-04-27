#include "talteen.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

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