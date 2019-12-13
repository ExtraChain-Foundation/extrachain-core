#ifndef SUBSCRIBE_CONTROLLER_H
#define SUBSCRIBE_CONTROLLER_H
#include "utils/db_connector.h"
#include "utils/utils.h"
#include <QByteArray>
#include <QObject>

class SubscribeController : public QObject
{
    Q_OBJECT
public:
    SubscribeController(QObject *parent = nullptr);
    SubscribeController(const SubscribeController &);
    ~SubscribeController();

public slots:
    void editMySubscribe(QByteArray id, QByteArray currentId, bool isRemove);
    void editMyFollower(QByteArray id, QByteArray currentId, bool isRemove);
    int checkCountSubscribe(QByteArray id);
    QList<std::string> getAllSubscribe(QByteArray id);

public:
    bool checkSubscribe(QByteArray id);
    QByteArray path;

    // get all (id, offset, count)
    // get count (id)
};
#endif // SUBSCRIBE_CONTROLLER_H
