#include "talteen.h"
#include "storageutil.h"

#include <QDir>
#include <QStorageInfo>

QString Talteen::getSdCardPath()
{
    return findSdCardMountPath();
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
