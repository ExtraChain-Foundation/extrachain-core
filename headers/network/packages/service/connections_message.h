#ifndef CONNECTTIONS_MESSAGE_H
#define CONNECTTIONS_MESSAGE_H
#include <vector>
#include "network/packages/base_message.h"
namespace Messages {
static const QByteArray ENABLE_LIST_CONNECTIONS = "connections";

struct ConnectionsMessage : ISmallMessage
{
    const short FIELD_SIZE = 3;
    const short FIELDS_COUNT = 1;
    std::vector<std::pair<std::string, int>> hosts;

public:
    // IMessage interface
    void operator=(const QByteArray &serialized);
    void operator=(QByteArray &serialized) override;
    void operator=(QList<QByteArray> &list);
    bool isEmpty() const override;
    QList<QByteArray> serializedParams() const;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};
}

#endif // CONNECTTIONS_MESSAGE_H
