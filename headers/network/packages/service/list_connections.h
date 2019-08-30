#ifndef LIST_CONNECTTIONS_H
#define LIST_CONNECTTIONS_H

#include "network/packages/base_message.h"
namespace Messages {
static const QByteArray ENABLE_LIST_CONNECTIONS = "connections";

class EnableConnections : public BaseMessage
{
    Q_OBJECT

    QList<std::pair<int, std::string>> enableConnections;

    const short FIELDS_COUNT = 1;

    short getFieldsCount() const override final;
    void initFields(QList<QByteArray> &list) override final;
    QList<QByteArray> serializedParams() const override final;

public:
    EnableConnections(const QByteArray &serialize);
    EnableConnections(const QList<std::pair<int, std::string>> &list);
    EnableConnections(const EnableConnections &connections);
    ~EnableConnections() override;

    QByteArray serialize() const override final;
    void deserialize(const QByteArray &serialized) override final;

    QList<std::pair<int, std::string>> getEnableConnections() const;
    EnableConnections operator=(const EnableConnections &value);

    QList<QByteArray> pairSerialize() const;
    void pairDesirialize(const QByteArray &serialized);
};
}

#endif // LIST_CONNECTTIONS_H
