#include "dfs/managers/headers/dfsindex.h"

void DfsIndex::createdDfsItemConnection(const DfsItem *dfsItem)
{
    connect(this, &DfsIndex::statusRequest, dfsItem, &DfsItem::sendStatus);
    connect(dfsItem, &DfsItem::sendStatus, this, &DfsIndex::dfsItemStatus);
}

void DfsIndex::appendsFromDirectory()
{
    QDir *dir = new QDir(based_dfs_struct::ROOT_FOOLDER_NAME + '/'
                         + accControler->getMainActor()->getId().toByteArray());
}

void DfsIndex::dfsSender(const QString &filePath, QString peerAdrress)
{
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);
    int bytes_count = 0;
    long long _file_size = file.size();
    if (_file_size < 513)
    {
        QByteArray data = file.readAll();
        Messages::DfsMessage msg(data, _file_size, filePath, 0, 1);
        if (peerAdrress.isEmpty())
            emit sendData(msg);
        else
            emit sendToUser(msg, peerAdrress);
    }
    else
    {
        const int _data_offset = 512;

        int g = _file_size / _data_offset;
        g += ((_file_size % _data_offset) != 0) ? 1 : 0;
        long long pos = 0;
        while ((pos + _data_offset) < _file_size)
        {
            char *ch = new char[_data_offset];
            file.read(ch, _data_offset);
            pos += _data_offset;
            Messages::DfsMessage msg(QByteArray(ch, _data_offset), _file_size, filePath, bytes_count, g);
            if (peerAdrress.isEmpty())
                emit sendData(msg);
            else
                emit sendToUser(msg, peerAdrress);
            delete[] ch;
            bytes_count++;
        }
        int _last_offset = _file_size - pos;
        char *ch = new char[_last_offset];
        file.read(ch, _last_offset);
        Messages::DfsMessage msg(QByteArray(ch, _last_offset), _file_size, filePath, bytes_count, g);
        //        msg.setNeedsByteCount(bytes_count);
        if (peerAdrress.isEmpty())
            emit sendData(msg);
        else
            emit sendToUser(msg, peerAdrress);
        delete[] ch;
    }
    file.close();
}

DfsIndex::DfsIndex(ActorIndex *actorIndex, AccountController *accountControler, QObject *parent)
    : QObject(parent)
    , accControler(accountControler)
    , actorIndex(actorIndex)
{
    QList<BigNumber> listUsers;
    for (BigNumber i = BigNumber(0); i < actorIndex->getLastSavedId(); i++)
        if (actorIndex->getActor(i).getAccount() && (i == accountControler->getMainActor()->getId()))
            listUsers.append(i);
    std::for_each(listUsers.begin(), listUsers.end(), [this](BigNumber userId) {
        Messages::DfsRequest rqst(DFS_REQUESTS::DFS_ALL, userId.toByteArray());
        emit sendRequest(rqst);
    });
    //    QList<QString> cardList = {};
    //    std::for_each(listUsers.begin(), listUsers.end(), [&cardList](BigNumber userId) {
    //        for (auto &el : based_dfs_struct::typesVec)
    //        {
    //            QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toString() + '/'
    //                + based_dfs_struct::toString(el) + based_dfs_struct::typeCardFilesMap[el];
    //            if (QFile(path).exists())
    //            {
    //                QFile f(path);
    //                f.open(QIODevice::ReadOnly);
    //                QByteArray data = f.readAll();
    //                if (!data.isEmpty())
    //                    cardList.append(path);
    //            }
    //        }
    //    });
    //    std::for_each(cardList.begin(), cardList.end(), [this](QString elPath) { dfsSender(elPath, ""); });
}

DfsIndex::DfsIndex(const DfsIndex &dfsIndex, QObject *parent)
    : QObject(parent)
{
    dfsItemList = dfsIndex.dfsItemList;
}

DfsIndex::~DfsIndex()
{
    for (DfsItem *el : dfsItemList)
    {
        el->~DfsItem();
        dfsItemList.removeOne(el);
    }
}

const DfsIndex DfsIndex::operator=(const DfsIndex &dfsIndex)
{
    dfsItemList = dfsIndex.dfsItemList;
    return *this;
}

bool DfsIndex::operator==(const DfsIndex &dfsIndex)
{
    return (dfsItemList == dfsIndex.dfsItemList);
}

int DfsIndex::changedData(const QString &path, const based_dfs_struct::Type &type,
                          const based_dfs_struct::SubType &subType, const based_dfs_struct::Status &status)
{
    //    initNewDfsItem(stored.getPath())->makeChanges(stored);
    initNewDfsItem(path, status);
    // emit makeChanges(serialize)
    //    initNewDfsItem(dir, based_dfs_struct::REPLACE);

    dfsSender(dfsItemList.last()->getPath(), "");
    emit usersChanged(dfsItemList.last()->getPath(), dfsItemList.last()->getType(),
                      dfsItemList.last()->getActorId());
    delete dfsItemList.last();
    return 0;
}

int DfsIndex::makeSystemDir(const BigNumber &userId) const
{
    QList<QByteArray> list;
    for (based_dfs_struct::Type type : based_dfs_struct::typesVec)
    {
        list << userId.toByteArray();
        list << based_dfs_struct::toByteArray(type);
        if (type == based_dfs_struct::images)
            for (based_dfs_struct::SubType subType : based_dfs_struct::subTypesVec)
            {
                if (list.isEmpty())
                    list << userId.toByteArray() << based_dfs_struct::toByteArray(based_dfs_struct::images);
                list << based_dfs_struct::toByteArray(subType);
                QString path = based_dfs_struct::ROOT_FOOLDER_NAME;
                QDir().mkdir(path);
                while (!list.isEmpty())
                {
                    path += '/' + QString(list.takeFirst());
                    QDir().mkdir(path);
                }
            }
        else
        {
            QString path = based_dfs_struct::ROOT_FOOLDER_NAME;
            while (!list.isEmpty())
            {
                path += '/' + QString(list.takeFirst());
                QDir().mkdir(path);
            }
        }
    }
    QFile(based_dfs_struct::ROOT_CARD_FILE_NAME).open(QIODevice::WriteOnly);
    QFile(based_dfs_struct::ROOT_CARD_FILE_NAME).close();
    return 0;
}

void DfsIndex::initNewDfsItem(const QString &path, based_dfs_struct::Status status)
{
    DfsItem *d = new DfsItem(path, status, actorIndex, accControler);
    dfsItemList.append(d); /* << new DfsItem(path, actorIndex, accControler);*/
    //    ThreadPool::addThread(d);
    CardManager::appendToCard(d->getType(), d->serialize(), d->getActorId());

    //    delete d;
    //    dfsItemList.last()->moveToThread();
    //    return dfsItemList.last();
}

QStringList DfsIndex::fileCompareAndReturnDifference(const QString &first, const QString &second) const
{
    QStringList difference;
    // only for card File from Dfs
    QFile file(first);
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();

    QList<QByteArray> rlist =
        Serialization::deserialize(data, Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER);
    QFile fileSecond(second);
    fileSecond.open(QIODevice::ReadOnly);
    data = fileSecond.readAll();

    QList<QByteArray> rlistSecond =
        Serialization::deserialize(data, Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER);

    for (int i = 0; i < rlist.size(); i++)
    {
        bool flag = false;
        for (int j = 0; j < rlistSecond.size(); j++)
            if (rlist[i] == rlistSecond[j])
                flag = true;
        if (!flag)
            difference.append(rlist[i]);
    }
    if (rlist.size() < rlistSecond.size())
        for (int i = rlist.size() - 1; i < rlistSecond.size(); i++)
            difference.append(rlistSecond[i]);
    return difference;
}

void DfsIndex::dfsItemStatus(bool status)
{
    if (status)
        dfsItemList.last()->~DfsItem();
}

void DfsIndex::getProfileById(QString userId)
{
    QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/'
        + based_dfs_struct::toString(based_dfs_struct::Type::servic) + "/profil.dat";
    if (!QFile(path).exists())
    {
        //        Messages::DfsRequest temp(DFS_REQUESTS::PROFILE_FILE_REQUEST,
        //                                  BigNumber(userId.toUtf8()), "");
        //        emit requestData(temp);
    }
    else
    {
        QFile *file = new QFile(path);
        file->open(QIODevice::ReadOnly);
        QByteArray data = file->readAll();
        emit sendProfile(userId, data);
        file->close();
        delete file;
    }
}
