#ifndef STATUS_H
#define STATUS_H

#include <QObject>
#include "dumessage.h"
namespace DFSMessage {

struct Status : public DUMessage
{
    const short FIELDS_COUNT = 3;

    QByteArray hash = "";
    QByteArray dirOwner = "";
    QStringList currentState;

    Status(const QByteArray &serialize);
    Status(const QByteArray &dirOwner, const QStringList &state);
    ~Status() override final;

    const QList<QByteArray> serializedParams() const override;

private:
    const QStringList deserializeState(const QByteArray &serialized);
    const QByteArray serializeState() const;
};
}
#endif // STATUS_H
