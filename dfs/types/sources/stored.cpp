#include "dfs/types/headers/stored.h"

Stored::Stored(const BigNumber actorId, const int first, const QByteArray changedata,
               const QByteArray sign, QByteArray path, QByteArray prevSig,
               QByteArray prevStoredHash, const storedSpace::State state)

{
    this->PrevSig = prevSig;
    this->PrevStoredHash = prevStoredHash;
    this->ChangeData = changedata;
    this->FirstByte = first;
    this->ChangeDataSig = sign;
    this->State = state;

    this->hash = Utils::calcKeccak(changedata);
    this->path = path;
    this->actorId = actorId;
}

Stored::~Stored()
{
}
Stored::Stored(const QByteArray &serialized)

{
    QList<QByteArray> list =
        Serialization::deserialize(serialized, Serialization::DFS_STORED_DELIMETR);
    if (list.count() == 9)
    {
        this->FirstByte = list.at(0).toInt();
        this->ChangeData = list.at(1);
        this->State = storedSpace::convertToDFSstate(list.at(2));
        this->ChangeDataSig = list.at(3);
        this->PrevSig = list.at(4);
        this->PrevStoredHash = list.at(5);

        this->hash = list.at(6);
        this->path = list.at(7);
        this->actorId = BigNumber(list.at(8));
    }
    else
        qDebug()
            << "STORED::"
            << "Stored(QByteArray &serialized), the list.count() not enought elements";
}
const Stored Stored::operator=(const Stored &temp)
{
    this->FirstByte = temp.FirstByte;
    this->ChangeData = temp.ChangeData;
    this->State = temp.State;
    this->ChangeDataSig = temp.ChangeDataSig;
    this->PrevSig = temp.PrevSig;
    this->PrevStoredHash = temp.PrevStoredHash;
    this->hash = temp.hash;
    this->path = temp.path;
    this->actorId = temp.actorId;
    return *this;
}

QByteArray Stored::serializedHeaderTail()
{
    QList<QByteArray> list = {};
    list << QString::number(this->FirstByte).toUtf8()
         << storedSpace::toString(this->State).toUtf8() << this->ChangeDataSig << this->PrevSig
         << this->PrevStoredHash << this->getHash() << this->getPath()
         << this->getAuthor().toByteArray();
    QByteArray headerData = Serialization::serialize(list, Serialization::DFS_STORED_DELIMETR);
    list.clear();
    list << headerData << this->ChangeData;
    return Serialization::serializeStored(list);
}
QByteArray Stored::serialized() const
{
    QList<QByteArray> list = {};
    list << QString::number(this->FirstByte).toUtf8() << this->ChangeData
         << storedSpace::toString(this->State).toUtf8() << this->ChangeDataSig << this->PrevSig
         << this->PrevStoredHash << this->getHash() << this->getPath()
         << this->getAuthor().toByteArray();
    return Serialization::serialize(list, Serialization::DFS_STORED_DELIMETR);
}

QByteArray Stored::serializedUserField() const
{
    return Serialization::serialize(
        { QString::number(this->FirstByte).toUtf8(), this->ChangeData,
          storedSpace::toString(this->State).toUtf8(), this->ChangeDataSig, this->PrevSig,
          this->PrevStoredHash, this->getHash(), this->getPath(),
          this->getAuthor().toByteArray() },
        Serialization::USER_FIELD_SPLITER);
}
//
Stored::Stored()
{
    this->FirstByte = -1;
    this->ChangeData = "EmptyData";
    this->State = storedSpace::State::UNRECOGS;
    this->ChangeDataSig = "EmptySig";
    this->PrevSig = "EmptyPrevSig";
    this->PrevStoredHash = "EmptyPrevStoredHash";
    this->hash = "EmptyHash";
    this->path = "EmptyPath";
    this->actorId = BigNumber("-1");
}

Stored::Stored(const Stored &_object)
{
    this->FirstByte = _object.getFirstByte();
    this->ChangeData = _object.getChangeData();
    this->State = _object.getState();
    this->ChangeDataSig = _object.getChangeDataSig();
    this->PrevSig = _object.getPrevSig();
    this->PrevStoredHash = _object.getPrevStoredHash();
    this->hash = _object.getHash();
    this->path = _object.getPath();
    this->actorId = _object.getAuthor();
}
void Stored::init(const QByteArray &serialized)
{
    QList<QByteArray> list =
        Serialization::deserialize(serialized, Serialization::DFS_STORED_DELIMETR);
    if (list.count() == 9)
    {
        this->FirstByte = list.at(0).toInt();
        this->ChangeData = list.at(1);
        this->State = storedSpace::convertToDFSstate(list.at(2));
        this->ChangeDataSig = list.at(3);
        this->PrevSig = list.at(4);
        this->PrevStoredHash = list.at(5);

        this->hash = list.at(6);
        this->path = list.at(7);
        this->actorId = BigNumber(list.at(8));
    }
    else
        qDebug()
            << "STORED::"
            << "Stored(QByteArray &serialized), the list.count() not enought elements";
}

int Stored::getFirstByte() const
{
    return this->FirstByte;
}
QByteArray Stored::getChangeData() const
{
    return this->ChangeData;
}
storedSpace::State Stored::getState() const
{

    return this->State;
}
QByteArray Stored::getChangeDataSig() const
{
    return this->ChangeDataSig;
}
QByteArray Stored::getPrevSig() const
{
    return this->PrevSig;
}
QByteArray Stored::getPrevStoredHash() const
{
    return this->PrevStoredHash;
}

QByteArray Stored::getPath() const
{
    return this->path;
}

BigNumber Stored::getAuthor() const
{
    return this->actorId;
}

QByteArray Stored::getStateBytes() const
{
    if (this->State == storedSpace::State::CHANGEDS)
        return "CHANGED";
    else if (this->State == storedSpace::State::NEWSTATE)
        return "CREATED";
    else if (this->State == storedSpace::State::DELSTATE)
        return "DELETED";
    return "UNRECOGNIZED";
}

void Stored::setChangeDataSig(QByteArray changeDataSig)
{
    this->ChangeDataSig = changeDataSig;
}

QByteArray Stored::getHash() const
{
    return this->hash;
}

// void Stored::calcStoredHash()
//{
//    this->setHash(Serialization::serialize(
//        { QString::number(this->FirstByte).toUtf8(), this->ChangeData,
//          storedSpace::toString(this->State).toUtf8(), this->ChangeDataSig,
//          this->PrevSig, this->PrevStoredHash, this->getName().toByteArray(),
//          this->getPath(), QString::number(this->getSize()).toUtf8(),
//          this->getAuthor().toByteArray(), this->getTime() },
//        Serialization::DFS_STORED_DELIMETR));
//}

// QByteArray Stored::serialize(const BigNumber actorId, const int first,
//                             const QByteArray changedata, const QByteArray sign,
//                             QByteArray path, QByteArray prevSig,
//                             QByteArray prevStoredHash,
//                             const storedSpace::State state) const
//{
//}
bool Stored::verify(const Actor<KeyPublic> &actor) const
{
    return this->getAuthor() == actor.getId();
}

// QByteArray Stored::serialize(BigNumber _author, storedSpace::State _state,
//                             QByteArray _changedata, QByteArray _path) const
//{
//    QList<QByteArray> l;
//    l << _author.toString().toLocal8Bit()
//      << storedSpace::toString(_state).toLocal8Bit() << _changedata << _path;
//    return Serialization::serialize(l, Serialization::ACTOR_FIELD_SPLITTER);
//}

// QByteArray Stored::getPrevFileChange() const
//{
//    return this->PrevFileChange;
//}

// QByteArray Stored::getPrevBlockHash() const
//{
//    return this->prevBlockshash;
//}

// QByteArray Stored::serialize() const
//{
//    return serialize(this->getAuthor(), this->getState(), this->getChangeData(),
//                     this->getPath());
//}

// QByteArray Stored::getChangeSig() const
//{
//    return this->ChangeSig;
//}

// QByteArray Stored::getPrevChangeSig() const
//{
//    return this->PrevChangeSig;
//}

// QByteArray Stored::getSign() const
//{
//    return this->sign;
//}
// QByteArray Stored::getData() const
//{
//    return this->data;
//}
