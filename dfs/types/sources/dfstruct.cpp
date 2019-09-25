#include "dfs/types/headers/dfstruct.h"

based_dfs_struct::State based_dfs_struct::convertToDFSstate(QByteArray _state)
{
    if (QString(_state) == "NEWSTATE")
        return State::NEWSTATE;
    else if (QString(_state) == "DELSTATE")
        return State::DELSTATE;
    else
        return State::CHANGEDS;
}

QString based_dfs_struct::toString(State _state)
{
    if (_state == CHANGEDS)
        return "CHANGEDS";
    else if (_state == NEWSTATE)
        return "NEWSTATE";
    else
        return "DELSTATE";
}

QByteArray based_dfs_struct::toByteArray(State _state)
{
    if (_state == CHANGEDS)
        return "CHANGEDS";
    else if (_state == NEWSTATE)
        return "NEWSTATE";
    else
        return "DELSTATE";
}
//
based_dfs_struct::Status based_dfs_struct::convertToDFSstatus(QByteArray state)
{
    if (QString(state) == "NEW")
        return Status::NEW;
    else if (QString(state) == "REPLACE")
        return Status::REPLACE;
    else
        return Status::MERGE;
}

QString based_dfs_struct::toString(Status _state)
{
    if (_state == REPLACE)
        return "EMPTY";
    else if (_state == MERGE)
        return "STATIC";
    else
        return "NEW";
}
QByteArray based_dfs_struct::toByteArray(Status _state)
{
    if (_state == REPLACE)
        return "EMPTY";
    else if (_state == MERGE)
        return "STATIC";
    else
        return "NEW";
}
//
based_dfs_struct::Type based_dfs_struct::convertToDFType(QByteArray type)
{
    if (QString(type) == "images")
        return Type::images;
    else if (QString(type) == "ivideo")
        return Type::ivideo;
    else if (QString(type) == "events")
        return Type::events;
    else if (QString(type) == "system")
        return Type::system;
    else if (QString(type) == "chates")
        return Type::chates;
    else if (type == "postes")
        return postes;
    else
        return Type::servic;
}

QByteArray based_dfs_struct::toByteArray(Type type)
{
    if (type == Type::images)
        return "images";
    else if (type == Type::ivideo)
        return "ivideo";
    else if (type == Type::events)
        return "events";
    else if (type == Type::system)
        return "system";
    else if (type == Type::chates)
        return "chates";
    else if (type == postes)
        return "postes";
    else
        return "servic";
}
QString based_dfs_struct::toString(Type type)
{
    if (type == Type::images)
        return "images";
    else if (type == Type::ivideo)
        return "ivideo";
    else if (type == Type::events)
        return "events";
    else if (type == Type::system)
        return "system";
    else if (type == Type::chates)
        return "chates";
    else if (type == postes)
        return "postes";
    else
        return "servic";
}
//
based_dfs_struct::SubType based_dfs_struct::convertToDFSSubType(QByteArray subType)
{
    if (subType == "profil")
        return profil;
    else if (subType == "avatar")
        return avatar;
    else if (subType == "ipost")
        return ipost;
    else if (subType == "mini")
        return mini;
    else if (subType == "portfolio")
        return portfolio;
    else
        return ievent;
}

QByteArray based_dfs_struct::toByteArray(SubType subType)
{
    if (subType == profil)
        return "profil";
    else if (subType == avatar)
        return "avatar";
    else if (subType == ipost)
        return "ipost";
    else if (subType == mini)
        return "mini";
    else if (subType == portfolio)
        return "portfolio";
    else
        return "ievent";
}
QString based_dfs_struct::toString(SubType subType)
{
    if (subType == profil)
        return "profil";
    else if (subType == avatar)
        return "avatar";
    else if (subType == ipost)
        return "ipost";
    else if (subType == mini)
        return "mini";
    else if (subType == portfolio)
        return "portfolio";
    else
        return "ievent";
}
based_dfs_struct::Key based_dfs_struct::convertToKey(QByteArray key)
{
    if (key == "dfsIndex")
        return Key::dfsIndex;
    else
        return Key::storedIndex;
}

QByteArray based_dfs_struct::toByteArray(Key key)
{
    if (key == dfsIndex)
        return "dfsIndex";
    else
        return "storedIndex";
}

QString based_dfs_struct::toString(Key key)
{
    if (key == dfsIndex)
        return "dfsIndex";
    else
        return "storedIndex";
}

based_dfs_struct::DfStruct::DfStruct(const based_dfs_struct::DfStruct &dfStruct)
{
    type = dfStruct.type;
    status = dfStruct.status;
    name = dfStruct.name;
    size = dfStruct.size;
    time = dfStruct.time;
    hash = dfStruct.hash;
    path = dfStruct.path;
    subType = dfStruct.subType;
    actorId = dfStruct.actorId;
}

based_dfs_struct::DfStruct::DfStruct(const QByteArray &serialized)
{
    QList<QByteArray> list = Serialization::deserialize(serialized, Serialization::DFS_DFSTRUCT_DELIMETR);
    if (list.size() != 10)
        return;
    type = based_dfs_struct::convertToDFType(list.at(6));
    status = based_dfs_struct::convertToDFSstatus(list.at(7));
    name = BigNumber(list.at(2));
    size = list.at(4).toLong();
    time = QDateTime::fromMSecsSinceEpoch(list.at(5).toLong());
    hash = list.at(1);
    path = list.at(3);
    subType = based_dfs_struct::convertToDFSSubType(list.at(9));
    actorId = BigNumber(list.at(8));
}

based_dfs_struct::DfStruct::DfStruct(const QString &_file_name, based_dfs_struct::Status status)
{
    //    if (!QFile(_file_name - based_dfs_struct::FILE_IDENTIFICATOR))
    if (status == based_dfs_struct::REPLACE)
    {
        QList<QByteArray> filePathList = Serialization::deserialize(_file_name.toUtf8() + '/', "/");
        actorId = BigNumber(filePathList.at(1));
        type = based_dfs_struct::convertToDFType(filePathList.at(2));
        if (type == based_dfs_struct::images)
        {
            subType = based_dfs_struct::convertToDFSSubType(filePathList.at(3));
            QByteArray t = filePathList.at(4);
            name = BigNumber(t.mid(0, t.size() - based_dfs_struct::FILE_IDENTIFICATOR.size()));
        }
        else
        {
            QByteArray t = filePathList.at(3);
            name = BigNumber(t.mid(0, t.size() - based_dfs_struct::FILE_IDENTIFICATOR.size()));
        }
        path = _file_name.mid(0, _file_name.size() - based_dfs_struct::FILE_IDENTIFICATOR.size()).toUtf8();

        QFile file(_file_name);
        QFile dfsFile(path);
        if (dfsFile.exists())
            dfsFile.remove();
        qDebug() << file.rename(path);
        size = file.size();
        time = QDateTime::currentDateTime();
        this->status = status;
        hash = Utils::calcKeccakForFile(path);
        file.close();
    }
    else if (status == based_dfs_struct::NEW)
    {
        int delimetrIndex = _file_name.indexOf('&');
        path = _file_name.mid(0, delimetrIndex).toUtf8();
        QList<QByteArray> filePathList = Serialization::deserialize(_file_name.toUtf8() + '/', "/");
        actorId = BigNumber(filePathList.at(1));
        type = based_dfs_struct::convertToDFType(filePathList.at(2));
        if (type == based_dfs_struct::images)
        {
            subType = based_dfs_struct::convertToDFSSubType(filePathList.at(3));
            QByteArray t = filePathList.at(4);
            name = BigNumber(t.mid(0, t.size() - based_dfs_struct::FILE_IDENTIFICATOR.size()));
        }
        else
        {
            QByteArray t = filePathList.at(3);
            name = BigNumber(t.mid(0, t.size() - based_dfs_struct::FILE_IDENTIFICATOR.size()));
        }
        QString tempPath = _file_name.mid(delimetrIndex + 1);
        QFile file(tempPath);
        file.open(QIODevice::ReadOnly);
        QFile dfsFile(path);
        dfsFile.open(QIODevice::WriteOnly | QIODevice::Truncate);

        long long _file_size = file.size();
        const int _data_offset = 512;
        long long pos = 0;

        while ((pos + _data_offset) < _file_size)
        {
            char *ch = new char[_data_offset];
            file.read(ch, _data_offset);
            dfsFile.write(ch, _data_offset);
            pos += _data_offset;
            delete[] ch;
        }
        int _last_offset = _file_size - pos;
        char *ch = new char[_last_offset];
        file.read(ch, _last_offset);
        dfsFile.write(ch, _last_offset);
        size = _file_size;
        time = QDateTime::currentDateTime();
        this->status = status;
        hash = Utils::calcKeccakForFile(path);
        dfsFile.flush();
        dfsFile.close();
        file.close();
        delete[] ch;
    }
    else
    {
        qDebug() << "[DfStruct] File:" << _file_name << "--status::" << status;
    }
}

BigNumber based_dfs_struct::DfStruct::getName() const
{
    return name;
}

void based_dfs_struct::DfStruct::setName(const BigNumber &value)
{
    name = value;
}

based_dfs_struct::Type based_dfs_struct::DfStruct::getType() const
{
    return type;
}

void based_dfs_struct::DfStruct::setType(const Type &value)
{
    type = value;
}

based_dfs_struct::Status based_dfs_struct::DfStruct::getStatus() const
{
    return status;
}

void based_dfs_struct::DfStruct::setStatus(const Status &value)
{
    status = value;
}

long long based_dfs_struct::DfStruct::getSize() const
{
    return size;
}

void based_dfs_struct::DfStruct::setSize(long long value)
{
    size = value;
}

QDateTime based_dfs_struct::DfStruct::getTime() const
{
    return time;
}

void based_dfs_struct::DfStruct::setTime(const QDateTime &value)
{
    time = value;
}

QByteArray based_dfs_struct::DfStruct::getHash() const
{
    return hash;
}

void based_dfs_struct::DfStruct::setHash(const QByteArray &value)
{
    hash = value;
}

QByteArray based_dfs_struct::DfStruct::getPath() const
{
    return path;
}

void based_dfs_struct::DfStruct::setPath(const QByteArray &value)
{
    path = value;
}

based_dfs_struct::SubType based_dfs_struct::DfStruct::getSubType() const
{
    return subType;
}

void based_dfs_struct::DfStruct::setSubType(const based_dfs_struct::SubType &value)
{
    subType = value;
}
QByteArray based_dfs_struct::DfStruct::getData() const
{
    return data;
}

void based_dfs_struct::DfStruct::setData(const QByteArray &value)
{
    data = value;
}
BigNumber based_dfs_struct::DfStruct::getActorId() const
{
    return actorId;
}

void based_dfs_struct::DfStruct::setActorId(const BigNumber &value)
{
    actorId = value;
}
based_dfs_struct::DfStruct based_dfs_struct::DfStruct::operator=(const based_dfs_struct::DfStruct &dfStruct)
{
    type = dfStruct.type;
    status = dfStruct.status;
    name = dfStruct.name;
    size = dfStruct.size;
    time = dfStruct.time;
    hash = dfStruct.hash;
    path = dfStruct.path;
    subType = dfStruct.subType;
    actorId = dfStruct.actorId;
    return *this;
}

bool based_dfs_struct::DfStruct::operator==(const based_dfs_struct::DfStruct &dfStruct)
{
    return ((hash == dfStruct.hash) && (size == dfStruct.size) && (time == dfStruct.time));
}

QByteArray based_dfs_struct::DfStruct::madeFolderDir(const based_dfs_struct::Type &type) const
{
    QList<QByteArray> list;
    list << ROOT_FOOLDER_NAME.toUtf8() << this->actorId.toByteArray() << toByteArray(type);
    return Serialization::serialize(list, '/');
}

// int based_dfs_struct::DfStruct::makeSystemDir(const BigNumber &userId)
//{
//    makeSystemDir();
//    //    if (QDir(ROOT_FOOLDER_NAME).exists())
//    //        return 1;
//    QDir().mkdir(ROOT_FOOLDER_NAME + '/' + userId.toString());
//    for (Type el : typesVec)
//        QDir().mkdir(ROOT_FOOLDER_NAME + '/' + userId.toString() + '/' + toString(el));
//    for (SubType el : subTypesVec)
//        QDir().mkdir(ROOT_FOOLDER_NAME + '/' + userId.toString() + '/' + toString(images) +
//        '/'
//                     + toString(el));
//    for (auto &el : cardFileConnections)
//    {
//        QFile *file = new QFile(ROOT_FOOLDER_NAME + '/' + userId.toString() + '/'
//                                + toString(el.first) + el.second);
//        el.second =
//            ROOT_FOOLDER_NAME + '/' + userId.toString() + '/' + toString(el.first) +
//            el.second;
//        if (!file->exists())
//        {
//            file->open(QIODevice::WriteOnly | QIODevice::Truncate);
//            file->flush();
//            file->close();
//            delete file;
//        }
//    }
//    return 0;
//}
