#include "talteen.h"

#include <QDir>
#include <QStorageInfo>

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
