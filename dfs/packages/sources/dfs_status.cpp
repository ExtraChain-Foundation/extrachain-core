#include "dfs/packages/headers/dfs_status.h"

std::vector<std::pair<std::string, std::string>> Messages::DfsStatus::getList() const
{
    return list;
}

BigNumber Messages::DfsStatus::getActorId() const
{
    return actorId;
}
short Messages::DfsStatus::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + 2;
}

void Messages::DfsStatus::initFields(QList<QByteArray> &list)
{
    BaseMessage::initFields(list);
    this->list = desirialize(list.takeFirst());
    actorId = BigNumber(list.takeFirst());
}

QList<QByteArray> Messages::DfsStatus::serializedParams() const
{
    QList<QByteArray> list = this->BaseMessage::serializedParams();
    list << QByteArray::fromStdString(serializeToStdString()) << actorId.toByteArray();
    return list;
}

Messages::DfsStatus::DfsStatus(const std::vector<std::pair<std::string, std::string>> &list,
                               BigNumber actorId)
{
    this->list = list;
    this->actorId = actorId;
}

Messages::DfsStatus::DfsStatus(const std::string &serialized)
{
    deserialize(QByteArray::fromStdString(serialized));
}

Messages::DfsStatus::DfsStatus(const Messages::DfsStatus &value)
{
    QList<QByteArray> list = value.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
    this->list = value.list;
    this->actorId = value.actorId;
}

Messages::DfsStatus::~DfsStatus()
{
}

Messages::DfsStatus Messages::DfsStatus::operator=(const Messages::DfsStatus &value)
{
    QList<QByteArray> list = value.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
    this->list = value.list;
    this->actorId = value.actorId;
    return *this;
}

QByteArray Messages::DfsStatus::serialize() const
{
    QByteArray serialize = BaseMessage::serialize(this->serializedParams());

    return serialize;
}

void Messages::DfsStatus::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> list = Serialization::universalDesirialize(serialized, FIELD_SIZES);
    initFields(list);
}

const std::string Messages::DfsStatus::serializeToStdString() const
{
    QList<QByteArray> elementList;
    std::for_each(list.begin(), list.end(), [&elementList](std::pair<std::string, std::string> el) {
        elementList.append(Serialization::serialize(
            { QByteArray::fromStdString(el.first), QByteArray::fromStdString(el.second) },
            Serialization::DEFAULT_LIST_SPLITTER));
    });
    std::string result =
        Serialization::serialize(elementList, Serialization::USER_FIELD_SPLITER).toStdString();
    return result;
}

const std::vector<std::pair<std::string, std::string>>
Messages::DfsStatus::desirialize(const QByteArray &data) const
{
    QList<QByteArray> list = Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
    std::vector<std::pair<std::string, std::string>> fileList;
    std::for_each(list.begin(), list.end(), [&fileList](QByteArray &el) {
        QList<QByteArray> segmentList = Serialization::deserialize(el, Serialization::DEFAULT_LIST_SPLITTER);
        if (segmentList.size() != 2)
        {
            qDebug() << segmentList << "incorrect data";
        }
        else
        {
            fileList.push_back(
                std::make_pair(segmentList.at(0).toStdString(), segmentList.at(1).toStdString()));
        }
    });
    return fileList;
}
