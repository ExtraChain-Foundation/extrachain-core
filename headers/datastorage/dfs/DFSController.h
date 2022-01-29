#pragma once

#include <QObject>

#include "datastorage/actor.h"

class /*EXTRACHAIN_EXPORT*/ DFSController : public QObject {
    Q_OBJECT
public:
    DFSController(const ActorId & actorId, QObject* parent = nullptr);

    void createDirectory();
    void initDB();

private:
    ActorId m_actorId;
};
