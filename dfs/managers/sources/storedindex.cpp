#include "dfs/managers/headers/storedindex.h"

StoredIndex::StoredIndex(ActorIndex *_actorIndex, AccountController *_accountController)
{
    this->s_ActorIndex = _actorIndex;
    this->account_contrlr = _accountController;
}
void StoredIndex::SendTempToVerify(QString path)
{
    QString croped_fileName = path;
    for (int i = path.size() - 1; i > 0; i--)
    {
        croped_fileName[i] = '\0';
        if (croped_fileName[i - 1] == '.')
        {
            croped_fileName[i - 1] = '\0';
            break;
        }
    }
    QFile file(croped_fileName);
    QFile filetemp(path);
    QByteArray tempFileData = "";
    if (file.exists())
        file.remove();

    if (filetemp.open(QIODevice::ReadOnly))
    {
        tempFileData = filetemp.readAll();
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(tempFileData);
            file.close();
        }
        else
        {
            qDebug() << "[Warning][storedIndex][SendTempToVerify] " + croped_fileName + " don't open";
        }
        filetemp.close();
    }
    else
    {
        qDebug() << "[Warning][storedIndex][SendTempToVerify] " + path + " don't open";
    }
}

///////////
/// \brief StoredIndex::addStored
/// Add stored to snapshots history of changing file. That create directory with apropriate name and heap of
/// stored represented by files with extension ".strd"
/// \param _stored  - structure of snapshot that need to
/// add in history. That containt info about changing.
/// \return 0 - if success. Else - error
///
int StoredIndex::addStored(const Stored &_stored)
{
    QDir storedDir(_stored.getPath());
    if (!storedDir.exists())
        if (!QDir().mkdir(storedDir.path()))
        {
            qDebug() << "[Error][storedIndex][addStored] Can't create directory for stored by path:"
                     << storedDir.path();
            return Errors::FILE_IS_NOT_OPENED;
        }
    QFile storageIndexInfo(_stored.getPath() + '/' + "storageInfo");
    BigNumber currentStoredIndex("0");
    if (storageIndexInfo.exists())
    {
        if (storageIndexInfo.open(QIODevice::ReadOnly))
        {
            currentStoredIndex = (BigNumber)storageIndexInfo.readLine();
            storageIndexInfo.close();
        }
        else
        {
            qDebug() << "[Error][storedIndex][addStored] Can't open file " << storageIndexInfo.fileName();
            return Errors::FILE_IS_NOT_OPENED;
        }
    }
    else
    {
        if (storageIndexInfo.open(QIODevice::WriteOnly))
        {
            storageIndexInfo.write("1");
            storageIndexInfo.close();
        }
        else
        {
            qDebug() << "[Error][storedIndex][addStored] Can't open file " << storageIndexInfo.fileName();
            return Errors::FILE_IS_NOT_OPENED;
        }
    }
    QFile file(storedDir.path() + '/' + currentStoredIndex.toByteArray() + ".strd");
    if (file.exists())
    {
        currentStoredIndex = searchCurrentStoredIndex(_stored.getPath());
        file.setFileName(storedDir.path() + '/' + currentStoredIndex.toByteArray() + ".strd");
    }
    QByteArray serialized = _stored.serializedUserField();
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(serialized);
        file.close(); // close the file handle.
    }
    else
    {
        qCritical() << "[Error][storedIndex][addStored] Can't open file " << file.fileName();
        return Errors::FILE_IS_NOT_OPENED;
    }
    if (storageIndexInfo.open(QIODevice::WriteOnly))
    {
        currentStoredIndex++;
        storageIndexInfo.write((currentStoredIndex).toByteArray());
        storageIndexInfo.close();
    }
    else
    {
        qDebug() << "[Error][storedIndex][addStored] Can't open file " << storageIndexInfo.fileName();
        return Errors::FILE_IS_NOT_OPENED;
    }

    return 0;
}

BigNumber StoredIndex::searchCurrentStoredIndex(QByteArray path)
{
    BigNumber i("0");
    while ((QFile(path + '/' + i.toByteArray() + ".strd").exists()))
    {
        i++;
    }
    return i;
}
/**
 * @brief Validates stored digital signature
 * @param _stored
 * @return true if stored is valid
 */
bool StoredIndex::validateStored(const Stored &_stored) const
{
    Actor<KeyPublic> actor = s_ActorIndex->getActor(_stored.getAuthor());
    if (actor.isEmpty())
    {
        qWarning() << "Can not validate stored with path" << _stored.getPath() << ": There no actor"
                   << _stored.getAuthor() << " in local storage";
        return false;
    }
    return _stored.verify(actor);
}
/// \brief getStoredByHash
/// \param path and hash
/// \return QList stored with the same hash
QList<Stored> StoredIndex::getStoredByHash(QByteArray path, QByteArray _hash) const
{
    QFile storageIndexInfo(path + '/' + "storageInfo");
    BigNumber currentStoredIndex("0");
    if (storageIndexInfo.exists())
    {
        if (storageIndexInfo.open(QIODevice::ReadOnly))
        {
            currentStoredIndex = (BigNumber)storageIndexInfo.readLine();
            storageIndexInfo.close();
        }
        else
        {
            qDebug() << "[Error][storedIndex][getStoredByHash] Can't open file "
                     << storageIndexInfo.fileName();
            return QList<Stored>();
        }
    }
    else
    {
        qDebug() << "[Error][storedIndex][getStoredByHash] storageInfo file doesn't exist.";
        return QList<Stored>();
    }

    QList<Stored> qlist_st;
    QFile file;
    QByteArray data;
    for (BigNumber i("0"); i < currentStoredIndex; i++)
    {
        file.setFileName(path + '/' + i.toByteArray() + ".strd");
        if (!file.exists())
        {
            qDebug() << "[Error][storedIndex][getStoredByHash] file" << file.fileName() + "doesn't exist.";
            return qlist_st;
        }

        if (file.open(QIODevice::ReadOnly))
        {

            data = file.readLine();

            QList<QByteArray> list = Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
            if (list.size() != 9)
            {
                qDebug() << "[Error][StoredIndex][getStoredByHash] List!=9";
                file.close();
                return qlist_st;
            }
            if (list.at(6) == _hash)
            {
                QList<QByteArray> l;
                for (int j = 0; j < 9; j++)
                    l << list.at(j);

                QByteArray serialized = Serialization::serialize(l, Serialization::DFS_STORED_DELIMETR);
                qlist_st.append(Stored(serialized));
            }
            data.clear();
        }
        file.close();
    }
    return qlist_st;
}
/// \brief getStoredByAuthor
/// \param _path
/// \return
QList<Stored> StoredIndex::getStoredByAuthor(QByteArray path, BigNumber _author) const
{
    QFile storageIndexInfo(path + '/' + "storageInfo");
    BigNumber currentStoredIndex("0");
    if (storageIndexInfo.exists())
    {
        if (storageIndexInfo.open(QIODevice::ReadOnly))
        {
            currentStoredIndex = (BigNumber)storageIndexInfo.readLine();
            storageIndexInfo.close();
        }
        else
        {
            qDebug() << "[Error][storedIndex][getStoredByAuthor] Can't open file "
                     << storageIndexInfo.fileName();
            return QList<Stored>();
        }
    }
    else
    {
        qDebug() << "[Error][storedIndex][getStoredByAuthor] storageInfo file doesn't exist.";
        return QList<Stored>();
    }
    QList<Stored> qlist_st;
    QFile file;
    QByteArray data;
    for (BigNumber i("0"); i < currentStoredIndex; i++)
    {
        file.setFileName(path + '/' + i.toByteArray() + ".strd");
        if (!file.exists())
        {
            qDebug() << "[Error][storedIndex][getStoredByAuthor] file" << file.fileName() + "doesn't exist.";
            return qlist_st;
        }

        if (file.open(QIODevice::ReadOnly))
        {

            data = file.readLine();

            QList<QByteArray> list = Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
            if (list.size() != 9)
            {
                qDebug() << "[Error][StoredIndex][getStoredByAuthor] List!=9";
                file.close();
                continue;
            }
            if (list.at(8) == _author)
            {
                QList<QByteArray> l;
                for (int j = 0; j < 9; j++)
                    l << list.at(j);

                QByteArray serialized = Serialization::serialize(l, Serialization::DFS_STORED_DELIMETR);
                qlist_st.append(Stored(serialized));
            }
            data.clear();
        }
        file.close();
    }
    return qlist_st;
}
///
/// \brief getStoredByPath
/// \param _path
/// \return
///
QList<Stored> StoredIndex::getStoredByPath(QByteArray _path) const
{
    QFile storageIndexInfo(_path + '/' + "storageInfo");
    BigNumber currentStoredIndex("0");
    if (storageIndexInfo.exists())
    {
        if (storageIndexInfo.open(QIODevice::ReadOnly))
        {
            currentStoredIndex = (BigNumber)storageIndexInfo.readLine();
            storageIndexInfo.close();
        }
        else
        {
            qDebug() << "[Error][storedIndex][getStoredByPath] Can't open file "
                     << storageIndexInfo.fileName();
            return QList<Stored>();
        }
    }
    else
    {
        qDebug() << "[Error][storedIndex][getStoredByPath] storageInfo file doesn't exist.";
        return QList<Stored>();
    }
    QList<Stored> qlist_st;
    QFile file;
    QByteArray data;
    for (BigNumber i("0"); i < currentStoredIndex; i++)
    {
        file.setFileName(_path + '/' + i.toByteArray() + ".strd");
        if (!file.exists())
        {
            qDebug() << "[Error][storedIndex][getStoredByPath] file" << file.fileName() + "doesn't exist.";
            return qlist_st;
        }

        if (file.open(QIODevice::ReadOnly))
        {

            data = file.readLine();

            QList<QByteArray> list = Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
            if (list.size() != 9)
            {
                qDebug() << "[Error][StoredIndex][getStoredByHash] List!=9 in stored numb " << i;
                file.close();
                continue;
            }

            QList<QByteArray> l;
            for (int j = 0; j < 9; j++)
                l << list.at(j);

            QByteArray serialized = Serialization::serialize(l, Serialization::DFS_STORED_DELIMETR);
            qlist_st.append(Stored(serialized));

            data.clear();
        }
        file.close();
    }
    return qlist_st;
}
QByteArray StoredIndex::calcChangeSig(QByteArray _changeData)
{
    return account_contrlr->getActor(account_contrlr->getUserNum()).getKey()->sign(_changeData);
}

Stored StoredIndex::getLastStoredByPath(QByteArray _path) const
{

    QFile storageIndexInfo(_path + '/' + "storageInfo");
    BigNumber currentStoredIndex("0");
    if (storageIndexInfo.exists())
    {
        if (storageIndexInfo.open(QIODevice::ReadOnly))
        {
            currentStoredIndex = (BigNumber)storageIndexInfo.readLine();
            currentStoredIndex--;
            storageIndexInfo.close();
        }
        else
        {
            qDebug() << "[Error][storedIndex][getLastStoredByPath] Can't open file "
                     << storageIndexInfo.fileName();
            return Stored();
        }
    }
    else
    {
        qDebug() << "[Error][storedIndex][getLastStoredByPath] storageInfo file doesn't exist.";
        return Stored();
    }
    QFile file;
    QByteArray data;

    file.setFileName(_path + '/' + currentStoredIndex.toByteArray() + ".strd");
    if (!file.exists())
    {
        qDebug() << "[Error][storedIndex][getLastStoredByPath] file" << file.fileName() + "doesn't exist.";
        return Stored();
    }

    if (file.open(QIODevice::ReadOnly))
    {
        data = file.readLine();
        QList<QByteArray> list = Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
        if (list.size() != 9)
        {
            qDebug() << "[Error][StoredIndex][getLastStoredByPath] List!=9";
            file.close();
            return Stored();
        }
        QList<QByteArray> l;
        for (int j = 0; j < 9; j++)
            l << list.at(j);
        QByteArray serialized = Serialization::serialize(l, Serialization::DFS_STORED_DELIMETR);
        file.close();
        return Stored(serialized);
    }
}
void StoredIndex::addStoredInIndex(Stored getStored)
{
    if (validateStored(getStored))
    {
        getStored.setChangeDataSig(calcChangeSig(getStored.getChangeData()));

        addStored(getStored);
    }
    else
        qDebug() << "Stored with actor id='" << getStored.getAuthor() << "' is not valid. Error add stored";
}

Stored StoredIndex::addSerializedStoredInIndex(QByteArray serialized)
{
    QList<QByteArray> qlist = Serialization::desirializeStored(serialized);
    if (qlist.size() != 2)
    {
        qDebug() << "Error in method addSerializedStoredInIndex, StoredIndex.";
    }
    QList<QByteArray> headerList = Serialization::deserialize(qlist[0], Serialization::DFS_STORED_DELIMETR);
    QByteArray actorSign =
        Utils::calcKeccak(qlist[1]); /*this->account_contrlr->getActor(BigNumber(headerList[0]))
                  .getKey()
                  ->sign(qlist[1]);*/
    QByteArray getPrevSig =
        Utils::calcKeccak(qlist[1]); /*this->getLastStoredByPath(headerList[3]).getChangeDataSig();*/

    //!!!!!!!!!!!!!!!!!!!!
    QByteArray getPrevStoredHash = this->getLastStoredByPath(headerList[3]).getHash();
    //    refactoring Assert List out of range

    if (headerList.size() != 5)
    {
        qDebug() << "Error in method addSerializedStoredInIndex, Header List don't have "
                    "4 parametres.";
    }

    // actor id, first byte, state, path
    Stored newStored(BigNumber(headerList[0]), headerList[1].toInt(), qlist[1], actorSign, headerList[3],
                     getPrevSig, "getPrevStoredHash", storedSpace::convertToDFSstate(headerList[2]));
    if (validateStored(newStored))
    {
        newStored.setChangeDataSig(calcChangeSig(newStored.getChangeData()));

        addStored(newStored);
    }
    else
        qDebug() << "Stored with actor id='" << newStored.getAuthor()
                 << "' is not valid. Error add stored addSerializedStoredInIndex";
    return newStored;
}

StoredIndex::~StoredIndex()
{
}

//            QByteArray data;
//            int sizeData=sizeof(file.readLine());
//          int j=1;
//           int currentPosition=file.size()-j*sizeData*8;
//            do
//            {

//                file.seek(currentPosition);
//                data=file.readLine();
//                if(data=="")
//                    return Stored();
//                QList<QByteArray> list = Serialization::deserialize(
//                            data, Serialization::ACTOR_FIELD_SPLITTER);
//                if(list.at(5)==_path)
//                {
//                    QList<QByteArray> l;
//                    for(int j=0;j<9;j++)
//                        l<<list.at(j);

//                    QByteArray serialized= Serialization::serialize(l,
//       Serialization::ACTOR_FIELD_SPLITTER); return Stored(serialized);
//                }
//                    j++;
//                    currentPosition-=j*sizeData;

//            }while(currentPosition>=0);

//            file.close();
//            return Stored();
//        }

// void StoredIndex::getChanged(BigNumber _author, based_dfs_struct::State _state,
//                             QByteArray _changedata, QByteArray _path, int
//                             _firstbyte)
//{
//    // if(validateStored(Stored(_author,_state,_changedata,_path,_firstbyte)))
//    //  addStored(Stored(_author,_state,_changedata,_path,_firstbyte));
//    emit NewStored(Stored(_author, _state, _changedata, _path, _firstbyte));
//}

// void StoredIndex::sgetStoredByPath(QHostAddress hostAddress, QByteArray storedPath)
//{
//    if (!getStoredByPath(storedPath).isEmpty())
//        emit StoredByPathFound(hostAddress, getStoredByPath(storedPath));
//    else
//        qDebug() << "The stored by path" << storedPath << " is not found";
//}

// void StoredIndex::sgetLastStoredByPath(QHostAddress hostAddress, QByteArray
// storedPath)
//{
//    if (getLastStoredByPath(storedPath).getPath() != "NULL")
//        emit LastStoredByPathFound(hostAddress, getLastStoredByPath(storedPath));
//    else
//        qDebug() << "The stored by path" << storedPath << " is not found";
//}

// void StoredIndex::sgetStoredByAuthor(QHostAddress hostAddress, BigNumber actorId)
//{
//    if (!getStoredByAuthor(actorId).isEmpty())
//        emit StoredByAuthorFound(hostAddress, getStoredByAuthor(actorId));
//    else
//        qDebug() << "The stored with author" << actorId << " is not found";
//}
// QByteArray StoredIndex::getPreviousHash()
//{
//    QFile file(this->path);

//    if (!file.exists())
//    {
//        qDebug() << "Can't get the file" << this->path << "(File is not exist)";
//        return "NULL";
//    }
//    if (file.open(QIODevice::ReadOnly))
//    {
//        QByteArray data;
//        while (!file.atEnd())
//            data = file.readLine();
//        QList<QByteArray> list =
//            Serialization::deserialize(data, Serialization::ACTOR_FIELD_SPLITTER);
//        file.close();
//        return list.at(1);
//    }
//    qDebug() << "There no stored with path:" << path;
//    return "NULL";
//}

// int StoredIndex::addDataToFile(QByteArray _data, QByteArray _path)
//{
//    QString path = QString(_path);
//    QFile file(path);

//    if (file.exists() && file.open(QIODevice::ReadWrite | QIODevice::Append))
//    {
//        file.seek(file.pos()); // get last position
//        file.write(_data);     // write the new text back to the file
//        file.close();          // close the file handle.
//        return 0;
//    }

//    if (file.open(QIODevice::WriteOnly))
//    {
//        QDataStream stream(&file);
//        stream << _data;
//        file.flush();
//        file.close();
//        return 0;
//    }
//    qCritical() << "Can't save the file" << path << "(File is not opened)";
//    return Errors::FILE_IS_NOT_OPENED;
//}

// int StoredIndex::changeDataInFile(int firstbyte, QByteArray _data, QByteArray
// _path)
//{
//    QString path = QString(_path);
//    QFile file(path);

//    if (!file.exists())
//    {
//        qDebug() << "Can't get the file" << path << "(File is not exits)";
//        return -1;
//    }

//    if (file.open(QIODevice::ReadWrite))
//    {
//        QByteArray fileData;
//        fileData = file.readAll(); // read all the data into the byte array
//        for (int i = firstbyte; i < firstbyte + _data.size(); i++)
//            fileData[i] = _data[i - firstbyte];

//        file.seek(0);         // go to the beginning of the file
//        file.write(fileData); // write the new text back to the file

//        file.close(); // close the file handle.
//        return 0;
//    }
//    qDebug() << "Can't get the file:" << path << "(File is not opened)";
//    return -1;
//}

// int StoredIndex::deleteDataFromFile(int firstbyte, int _size, QByteArray _path)
//{
//    QString path = QString(_path);
//    QFile file(path);

//    if (!file.exists())
//    {
//        qDebug() << "Can't get the file" << path << "(File is not exits)";
//        return -1;
//    }

//    if (file.open(QIODevice::ReadWrite))
//    {
//        QByteArray fileData;
//        fileData = file.readAll(); // read all the data into the byte array
//        fileData.remove(firstbyte, _size);

//        file.seek(0);         // go to the beginning of the file
//        file.write(fileData); // write the new text back to the file

//        file.close(); // close the file handle.
//        return 0;
//    }
//    qDebug() << "Can't get the file:" << path << "(File is not opened)";
//    return -1;
//}
