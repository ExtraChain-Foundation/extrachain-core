#ifndef SUBSCRIBE_CONTROLLER_H
#define SUBSCRIBE_CONTROLLER_H

#include <QObject>
#include <QByteArray>

#include "utils/db_connector.h"
#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"

class NodeManager;

class SubscribeController : public QObject
{
    Q_OBJECT

public:
    SubscribeController(QObject *parent = nullptr);
    SubscribeController(const SubscribeController &);
    ~SubscribeController();

signals:
    void send(int saveType, QString file, QByteArray data, const DfsStruct::Type type);
    void sendEditSql(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                     QByteArrayList sqlChanges);

public slots:
    void editMySubscribe(QByteArray id, bool isRemove);
    int checkCountSubscribe(QByteArray id);
    std::vector<DBRow> getAllSubscribe(QByteArray id);

public:
    bool checkSubscribe(QByteArray id);
    void setNodeManager(NodeManager *value);

private:
    NodeManager *nodeManager;

    // get all (offset, count)
};
#endif // SUBSCRIBE_CONTROLLER_H
