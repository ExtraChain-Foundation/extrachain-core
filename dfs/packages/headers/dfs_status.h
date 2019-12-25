#ifndef DFS_STATUS_H
#define DFS_STATUS_H
#include "network/packages/base_message.h"
namespace Messages {

// static const QByteArray DFS_STATUS_MESSAGE = "dfsStatusMessage";

class DfsStatus : public BaseMessage
{
private:
    BigNumber actorId;
    std::vector<std::pair<std::string, std::string>> list; // list with hash and filePath;

    short getFieldsCount() const override;
    void initFields(QList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;

public:
    DfsStatus(const std::vector<std::pair<std::string, std::string>> &list, BigNumber actorId);
    DfsStatus(const std::string &serialized);
    DfsStatus(const DfsStatus &value);
    ~DfsStatus() override;

    DfsStatus operator=(const DfsStatus &value);
    QByteArray serialize() const override final;
    void deserialize(const QByteArray &serialized) override;

    const std::string serializeToStdString() const;
    const std::vector<std::pair<std::string, std::string>> desirialize(const QByteArray &data) const;
    std::vector<std::pair<std::string, std::string>> getList() const;
    BigNumber getActorId() const;
};
}
#endif // DFS_STATUS_H
