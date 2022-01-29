#include "datastorage/dfs/DFSController.h"

#include <QDir>
#include <QFileInfo>

#include <QDebug>

static const QString DfsRootDirName = "dfs";

DFSController::DFSController(const ActorId &actorId, QObject* parent)
    : QObject(parent)
    , m_actorId(actorId)
{
    createDirectory();
    initDB();
}

void DFSController::createDirectory()
{
    qDebug() << "DFSController: createDirectory";

    auto createSubDirectory = [] (const QString & parentDirStr, const QString & subDirStr) {
        QString destPathStr = QDir::cleanPath(parentDirStr + "/" + subDirStr);
        QDir parentDir(parentDirStr);
        if (!parentDir.exists(subDirStr)) {
            if (!parentDir.mkdir(subDirStr)) {
                destPathStr = "";
            }
        }
        return destPathStr;
    };

    QString dfsRootDirPath = createSubDirectory(QDir::homePath(), DfsRootDirName);
    if (dfsRootDirPath.isEmpty()) {
        qDebug() << "DFSController: createDirectory: DFS root dir create error:" << dfsRootDirPath;
        QCoreApplication::exit(-2);
    }

    QString dfsUserDirPath = createSubDirectory(dfsRootDirPath, m_actorId.toString());
    if (dfsUserDirPath.isEmpty()) {
        qDebug() << "DFSController: createDirectory: DFS user dir create error:" << dfsUserDirPath;
        QCoreApplication::exit(-3);
    }
}
