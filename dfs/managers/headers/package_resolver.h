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
    QMap<QByteArray, long long> counterPckg;
    QMap<QByteArray, QString> queueFiles; //
    QMap<QString, QByteArray> fileMap;    // tmp with real path

    QMap<QByteArray, QFile *> listFile = {};

    bool titleMsg(const Message::title_message &msg);

public:
    DFSResolver(ActorIndex *actorIndex, QObject *parent = nullptr);
    ~DFSResolver();

    void validate();
    bool isActive() const;
    bool createTempFile(const QString &path, const long long &size, const QByteArray &tHash);

signals:
    void save(const QString tmpPath, const QString &path, const based_dfs_struct::Type &type);
    void checkStatus(const QByteArray &actorId, const QStringList &request, const SocketPair &receiver);
    void closingMsg(const QByteArray &titleHash, const long long &pckAF, const SocketPair &receiver);
    void startTimerD(const long long &size, const QString &path, const QByteArray &titleS);
    void finished();

public slots:
    void receiveMsg(const QByteArray &msg, int msgType, const SocketPair &receiver);
    void process();
};
#endif
