#include "accessmediator.h"

#include <QDir>
#include <QDirIterator>

#include <unistd.h>

AccessMediator::AccessMediator(QObject *parent) : QObject(parent)
{

}

QVector<quint64> AccessMediator::findUsingPids(const QString &device)
{
    QVector<quint64> pids;

    QDirIterator dirIt(QStringLiteral("/proc"), QStringList() << "fd", QDir::Dirs, QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        const QString path = dirIt.next();

        // Skip ourselves
        if (path == QStringLiteral("/proc/%1/fd").arg(getpid()))
            continue;

        // Skip proc entries of other users
        if (!dirIt.fileInfo().isReadable())
            continue;

        // Get the pid
        QStringList pathSplit = path.split('/');
        if (pathSplit.length() < 2)
            continue;
        bool parseOk = false;
        quint64 pid = pathSplit[1].toULongLong(&parseOk);
        if (!parseOk)
            continue;

        // Find all open files for the process
        QDirIterator procIt(path, QDir::Files, QDirIterator::FollowSymlinks);
        while (procIt.hasNext()) {
            const QString file = procIt.next();

            if (file == device) {
                pids.push_back(pid);
            }
        }
    }

    return pids;
}
