#include "storageutil.h"

#include <QStorageInfo>

QString findSdCardMountPath()
{
    QString best;
    int bestDepth = 0;

    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes())
    {
        if (!storage.isValid() || !storage.isReady() || storage.isReadOnly())
            continue;

        const QString path = storage.rootPath();
        if (!path.startsWith(QStringLiteral("/run/media/")))
            continue;

        // Skip /run/media/<user> — not the card mount itself
        const int depth = path.count(QLatin1Char('/'));
        if (depth < 4)
            continue;

        if (depth > bestDepth)
        {
            bestDepth = depth;
            best = path;
        }
    }

    return best;
}
