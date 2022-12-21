#include "datastorage/threds/inserter_files.h"
#include "datastorage/dfs/dfs_controller.h"
#include <QFileInfo>

InserterFiles::InserterFiles(ExtraChainNode &xNode, const QStringList &files, QObject *parent)
    : QThread(parent)
    , node(xNode)
    , fList(files) {
}

void InserterFiles::run() {
    for (const auto &filePath : fList) {
        qDebug() << "Files add in thread id: [" << QThread::currentThreadId() << "]";
        const std::string result =
            node.dfs()->addLocalFile(node.accountController()->mainActor(), filePath.toStdWString(),
                                     QFileInfo(filePath).fileName().toStdString(), DFS::Encryption::Public);
        emit resultAddFile(QString::fromStdString(result), filePath);
    }
}
