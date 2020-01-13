#ifndef STATUS_H
#define STATUS_H

#include <QObject>
#include "utils/utils.h"
#include "dfs/packages/headers/dfs_message_interface.h"
#include "headers/network/packages/message_interface.h"

namespace DistFileSystem {

struct Status : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 3;

    QByteArray hash = "";
    QByteArray dirOwner = "";
    QStringList currentState;

    const QList<QByteArray> serializedParams() const;
    void calcHash();

private:
    const QStringList deserializeState(const QByteArray &serialized);
    const QByteArray serializeState() const;

    // ISmallMessage interface
public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};
}
#endif // STATUS_H
