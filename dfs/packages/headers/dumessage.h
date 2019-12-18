#ifndef DUMESSAGE_H
#define DUMESSAGE_H
#include "dfs_message_interface.h"
namespace DFSMessage {

class DUMessage : public IDfs_Message
{
    const short FIELDS_COUNT = 1;

protected:
    dfsMessageType type;

public:
    DUMessage(QObject *parent = nullptr);
    DUMessage(const int &type, QObject *parent = nullptr);
    DUMessage(const QByteArray &serialized, QObject *parent = nullptr);
    ~DUMessage() = default;

    const QByteArray hash() const override;
    const QByteArray serialize() const override;
    int getType() const;
    bool isEmpty() const;

protected:
    const QList<QByteArray> serializedParams() const override;
    const QList<QByteArray> deserialize(const QByteArray &serialized) override;
    const QByteArray concatenate() const override;
};
}
#endif // DUMESSAGE_H
