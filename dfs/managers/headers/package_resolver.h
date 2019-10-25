#ifndef PACKAGE_RESOLVER_H
#define PACKAGE_RESOLVER_H

#include "dfs/managers/headers/dfsindex.h"

#include "dfs/packages/headers/dfs_message_interface.h"
#include "dfs/packages/headers/all.h"

class DFSResolver : public QObject
{
    Q_OBJECT

private:
    ActorIndex *actorIndex;
    bool active = false;
    QByteArray msg;
    QByteArray hash;
    QMap<QByteArray, long long> counterPckg;
    QMap<QByteArray, QString> queueFiles; //
    QMap<QString, QByteArray> fileMap;    // tmp with real path

public:
    DFSResolver(ActorIndex *actorIndex, QObject *parent = nullptr);
    ~DFSResolver();

    void validate();
    bool isActive() const;
    bool createTempFile(const QString &path, const long long &size);
    void receiveMsg(const QByteArray &msg, int msgType, const SocketPair &receiver);
signals:
    void save(const QString tmpPath, const QString &path, const based_dfs_struct::Type &type);
    void finished();

public slots:
    void process();
};
#endif
