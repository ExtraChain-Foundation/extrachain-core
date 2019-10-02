#include "dfs/types/headers/dfsitem.h"

DfsItem::DfsItem(const DfsItem &dfsItem, QObject *parent)
    : QObject(parent)
    , based_dfs_struct::DfStruct(dfsItem.serialize())
{
    storedIndex = dfsItem.storedIndex;
    status = dfsItem.status;
}

DfsItem::DfsItem(QByteArray &serialized, QObject *parent)
    : QObject(parent)
    , based_dfs_struct::DfStruct(serialized)
{
}

DfsItem::DfsItem(const QString &fileName, based_dfs_struct::Status status, QObject *parent)
    : QObject(parent)
    , based_dfs_struct::DfStruct(fileName, status)
{
}
DfsItem::DfsItem(const QString &path, based_dfs_struct::Status status, ActorIndex *actorIndex,
                 AccountController *accountControler, const QByteArray &data, QObject *parent)
    : QObject(parent)
    , based_dfs_struct::DfStruct(path, status)
{
    if (this->getType() == 2 || this->getType() == 5 || this->getType() == 6)
    {
        storedIndex = new StoredIndex(actorIndex, accountControler);
        Stored stored(this->getActorId(), 0, data, this->getHash(), this->getPath(), "", "",
                      this->getStatus() == 0 ? storedSpace::State::NEWSTATE : storedSpace::State::CHANGEDS);
        storedIndex->addStoredInIndex(stored);
    }
}
DfsItem::DfsItem(const QString &path, based_dfs_struct::Status status, ActorIndex *actorIndex,
                 AccountController *accountControler, QObject *parent)
    : QObject(parent)
    , based_dfs_struct::DfStruct(path, status)
{
    storedIndex = new StoredIndex(actorIndex, accountControler);
}
// DfsItem::DfsItem(StoredIndex *storedIndex, QObject *parent)
//    : QObject(parent)
//    , based_dfs_struct::DfStruct()
//    , storedIndex(storedIndex)
//{
//}

DfsItem::~DfsItem()
{
    emit finish();
}

const DfsItem DfsItem::operator=(const DfsItem &dfsItem)
{
    storedIndex = dfsItem.storedIndex;
    status = dfsItem.status;
    return *this;
}

bool DfsItem::operator==(const DfsItem &dfsItem)
{
    return ((storedIndex == dfsItem.storedIndex) && (status == dfsItem.status));
}

const QByteArray based_dfs_struct::DfStruct::serialize() const
{
    QList<QByteArray> list;
    list << this->based_dfs_struct::DfStruct::getData() << this->based_dfs_struct::DfStruct::getHash()
         << this->based_dfs_struct::DfStruct::getName().toByteArray()
         << this->based_dfs_struct::DfStruct::getPath()
         << QString::number(this->based_dfs_struct::DfStruct::getSize()).toUtf8()
         << QString::number(this->based_dfs_struct::DfStruct::getTime().toMSecsSinceEpoch()).toUtf8()
         << based_dfs_struct::toByteArray(this->based_dfs_struct::DfStruct::getType())
         << based_dfs_struct::toByteArray(this->based_dfs_struct::DfStruct::getStatus())
         << this->based_dfs_struct::DfStruct::getActorId().toByteArray()
         << toByteArray(this->based_dfs_struct::DfStruct::getSubType());
    return Serialization::serialize(list, Serialization::DFS_DFSTRUCT_DELIMETR);
}
const QByteArray DfsItem::serialize() const
{
    return based_dfs_struct::DfStruct::serialize();
}

StoredIndex *DfsItem::getStoredIndex() const
{
    return storedIndex;
}

void DfsItem::setStoredIndex(StoredIndex *value)
{
    storedIndex = value;
}

based_dfs_struct::Type DfsItem::getType() const
{
    return this->based_dfs_struct::DfStruct::getType();
}

based_dfs_struct::Status DfsItem::getStatus() const
{
    return this->based_dfs_struct::DfStruct::getStatus();
}

BigNumber DfsItem::getName() const
{
    return this->based_dfs_struct::DfStruct::getName();
}

long long DfsItem::getSize() const
{
    return this->based_dfs_struct::DfStruct::getSize();
}

QDateTime DfsItem::getTime() const
{
    return this->based_dfs_struct::DfStruct::getTime();
}

QByteArray DfsItem::getHash() const
{
    return this->based_dfs_struct::DfStruct::getHash();
}

QByteArray DfsItem::getPath() const
{
    return this->based_dfs_struct::DfStruct::getPath();
}

based_dfs_struct::SubType DfsItem::getSubType() const
{
    return this->based_dfs_struct::DfStruct::getSubType();
}

QByteArray DfsItem::getData() const
{
    return this->based_dfs_struct::DfStruct::getData();
}

BigNumber DfsItem::getActorId() const
{
    return this->based_dfs_struct::DfStruct::getActorId();
}

const QString DfsItem::makeDir() const
{
    QList<QByteArray> list;
    list << based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() << this->getActorId().toByteArray()
         << toByteArray(this->getType());

    list.append(this->getType() == based_dfs_struct::images ? toByteArray(this->getSubType())
                                                            : this->getName().toByteArray());
    QString path = QString(Serialization::serialize(list, '/'));
    path.remove(path.length() - 1, 1);
    return path;
}

const QString DfsItem::makeDir(const DfsItem *dfsItem) const
{
    QList<QByteArray> list;
    list << based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() << dfsItem->getActorId().toByteArray()
         << toByteArray(dfsItem->getType());

    list.append(dfsItem->getType() == based_dfs_struct::images ? toByteArray(dfsItem->getSubType())
                                                               : dfsItem->getName().toByteArray());
    QString path = QString(Serialization::serialize(list, '/'));
    path.remove(path.length() - 1, 1);
    return path;
}

int DfsItem::makeChanges(QByteArray data)
{
    QByteArray header = Serialization::desirializeStored(data).at(0);
    QList<QByteArray> dataHeaderList = Serialization::deserialize(header, Serialization::DFS_STORED_DELIMETR);
    //    Stored stored = storedIndex->addSerializedStoredInIndex(data);
    QFile file(dataHeaderList.at(3));
    //    int sizeData = 0;
    if (storedSpace::convertToDFSstate(dataHeaderList.at(2)) != storedSpace::NEWSTATE)
    {
    }
    else
    {
        file.close();
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        file.write(Serialization::desirializeStored(data).at(1));
        file.flush();
        file.close();
        Stored stored = storedIndex->addSerializedStoredInIndex(data);
        QByteArray dfsItemType = Serialization::deserialize(stored.getPath(), "/").at(2);
        this->setStatus(based_dfs_struct::NEW);
        this->setSize(stored.getChangeData().size());
        this->setName(CardManager::getNameForNewFile(based_dfs_struct::convertToDFType(dfsItemType)));
        this->setTime(QDateTime::currentDateTime());
        this->setHash(Utils::calcKeccak(stored.getChangeData()));
        this->setPath(stored.getPath());
        this->setType(based_dfs_struct::convertToDFType(dfsItemType));
        this->setActorId(stored.getAuthor());
    }
    QList<QByteArray> list = Serialization::deserialize(this->getPath().append("/"), "/");
    if (list.size() > 3)
    {
    }
    return 0;
}
void Subscribtion::add(const BigNumber &actorId)
{
    QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + ownerId.toString() + fileName;
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    file.write(actorId.toByteArray());
    file.flush();
    file.close();
}

void Subscribtion::remove(const BigNumber &actorId)
{
    QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + ownerId.toString() + fileName;
    QFile file(path);
    file.open(QIODevice::ReadWrite);
    while (!file.atEnd())
    {
        if (BigNumber(file.read(20)) == actorId)
        {
            QByteArray data = file.readAll();
            file.seek(file.pos() - 20);
            file.write(data);
            file.flush();
            file.close();
            return;
        }
    }
    file.flush();
    file.close();
}

QList<BigNumber> Subscribtion::getAll() const
{
    QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + ownerId.toString() + fileName;
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    QList<BigNumber> list;
    while (!file.atEnd())
    {
        list.append(BigNumber(file.read(20)));
    }
    file.close();
    return list;
}
