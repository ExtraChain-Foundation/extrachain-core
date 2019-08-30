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
            qDebug() << "[Warning][storedIndex][SendTempToVerify] " + croped_fileName
                    + " don't open";
        }
        filetemp.close();
    }
    else
    {
        qDebug() << "[Warning][storedIndex][SendTempToVerify] " + path + " don't open";
    }
}

int StoredIndex::addStored(const Stored &_stored)
{
    QFile file(_stored.getPath() + ".strd");
    //    l << _stored.getAuthor().toString().toLocal8Bit() << _stored.getHash()
    //      << based_dfs_struct::toString(_stored.getState()).toLocal8Bit()
    //      << _stored.getChangeData() << calcChangeSig(_stored.getChangeData())
    //      << _stored.getPath() /*<< getPreviousHash()*/
    //      << getLastStoredByPath(_stored.getPath()).getChangeSig()
    //      << QByteArray::number(_stored.getFirstByte());
    //    l << _stored.getHash() << _stored.getData() << _stored.getPath()
    //      << QString::number(_stored.getFirstByte()).toLocal8Bit() <<
    //      /*_stored.getSign()*/
    //      /*<<*/ _stored.getPrevBlockHash() << _stored.getStateBytes() <<
    //      _stored.getChangeData()
    //      << /*_stored.getChangeSig() << _stored.getPrevChangeSig()*/
    //     /* <<*/ _stored.getPrevFileChange();

    QByteArray serialized = _stored.serializedUserField();
    // serialized.append('\n');
    serialized.append(DELIM);
    if (file.exists())
    {
        if (file.open(QIODevice::ReadWrite | QIODevice::Append))
        {
            file.seek(file.pos()); // seek end position
            file.write(serialized);
            file.close(); // close the file handle.
        }
        else
        {
            qCritical() << "Can't save the file" << _stored.getPath()
                        << "(File is not opened)";
            return Errors::FILE_IS_NOT_OPENED;
        }
    }

    if (!file.exists())
    {
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(serialized);
            file.close(); // close the file handle.
        }
        else
        {
            qCritical() << "Can't save the file" << _stored.getPath()
                        << "(File is not opened)";
            return Errors::FILE_IS_NOT_OPENED;
        }
    }
    return 0;

    /*  if (_stored.getState() == based_dfs_struct::CREATED)
      {
          this->addDataToFile(_stored.getChangeData(), _stored.getPath());
          return 0;
      }
      else if (_stored.getState() == based_dfs_struct::CHANGED)
      {
          this->changeDataInFile(_stored.getFirstByte(), _stored.getChangeData(),
                                 _stored.getPath());
          return 0;
      }
      else if (_stored.getState() == based_dfs_struct::DELETED)
      {
          this->deleteDataFromFile(_stored.getFirstByte(),
          _stored.getChangeData().size(),
                                   _stored.getPath());
          return 0;
      }
      else
      {
          return based_dfs_struct::UNRECOGNIZED;
      }
   SIZE = size changed data in stored. For instance if need to delete 4 bytes, need
      to
   send ChangedData in Stored with size by 4 bytes "1234" for instance, that enough*/
}

bool StoredIndex::validateStored(const Stored &_stored) const
{
    Actor<KeyPublic> actor = s_ActorIndex->getActor(_stored.getAuthor());
    if (actor.isEmpty())
    {
        qWarning() << "Can not validate stored with path" << _stored.getPath()
                   << ": There no actor" << _stored.getAuthor() << " in local storage";
        return false;
    }
    return _stored.verify(actor);
}

QList<Stored> StoredIndex::getStoredByHash(QByteArray path, QByteArray _hash) const
{
    QList<Stored> qlist_st;
    QFile file(path + ".strd");

    if (!file.exists())
    {
        qDebug() << "Can't get the file" << path << "(File is not exist)";
        return qlist_st;
    }
    //   int quant=0;
    // QStack<Stored> StoredStack;
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray data;
        int pos = -1;
        QString resultLine = "";
        while (pos + DELIM.size() < file.size())
        {
            pos++;
            file.seek(pos);
            resultLine.append(file.read(1));
            if (file.read(DELIM.size()) != DELIM)
            {
                continue;
            }

            data = resultLine.toUtf8();

            QList<QByteArray> list =
                Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
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

                QByteArray serialized =
                    Serialization::serialize(l, Serialization::INFORMATION_SEPARATOR_ONE);
                qlist_st.append(Stored(serialized));
            }
            data.clear();
            resultLine.clear();
            pos += DELIM.size();

            //  QList<QByteArray> list =
            //    Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
            // if (list.at(7) == _hash)
            //            {
            //                QList<QByteArray> l;
            //                for (int j = 0; j < 12; j++)
            //                    l << list.at(j);

            //                QByteArray serialized =
            //                    Serialization::serialize(l,
            //                    Serialization::INFORMATION_SEPARATOR_ONE);
            //                qlist_st.append(Stored(serialized));
            //            }
            //            else
            //                continue;
        }
        file.close();
        return qlist_st;
    }

    qDebug() << "[Error][StoredIndex][getStoredByHash] File not open" << path + ".strd";
    return qlist_st;
}

QList<Stored> StoredIndex::getStoredByAuthor(QByteArray path, BigNumber _author) const
{
    QList<Stored> qlist_st;
    QFile file(path + ".strd");

    if (!file.exists())
    {
        qDebug() << "Can't get the file" << path << "(File is not exist)";
        return qlist_st;
    }
    //   int quant=0;
    // QStack<Stored> StoredStack;
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray data;
        int pos = -1;
        QString resultLine = "";
        while (pos + DELIM.size() < file.size())
        {
            pos++;
            file.seek(pos);
            resultLine.append(file.read(1));
            if (file.read(DELIM.size()) != DELIM)
            {
                continue;
            }

            data = resultLine.toUtf8();

            QList<QByteArray> list =
                Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
            if (list.size() != 9)
            {
                qDebug() << "[Error][StoredIndex][getStoredByAuthor] List!=9";
                file.close();
                return qlist_st;
            }
            if (list.at(8) == _author)
            {
                QList<QByteArray> l;
                for (int j = 0; j < 9; j++)
                    l << list.at(j);

                QByteArray serialized =
                    Serialization::serialize(l, Serialization::INFORMATION_SEPARATOR_ONE);
                qlist_st.append(Stored(serialized));
            }
            data.clear();
            resultLine.clear();
            pos += DELIM.size();

            //  QList<QByteArray> list =
            //    Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
            // if (list.at(7) == _hash)
            //            {
            //                QList<QByteArray> l;
            //                for (int j = 0; j < 12; j++)
            //                    l << list.at(j);

            //                QByteArray serialized =
            //                    Serialization::serialize(l,
            //                    Serialization::INFORMATION_SEPARATOR_ONE);
            //                qlist_st.append(Stored(serialized));
            //            }
            //            else
            //                continue;
        }
        file.close();
        return qlist_st;
    }

    qDebug() << "[Error][StoredIndex][getStoredByAuthor] File not open" << path + ".strd";
    return qlist_st;
}

QList<Stored> StoredIndex::getStoredByPath(QByteArray _path) const
{
    QList<Stored> qlist_st;
    QFile file(_path + ".strd");

    if (!file.exists())
    {
        qDebug() << "Can't get the file" << _path << "(File is not exist)";
        return qlist_st;
    }
    //   int quant=0;
    // QStack<Stored> StoredStack;
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray data;
        int pos = -1;
        QString resultLine = "";
        while (pos + DELIM.size() < file.size())
        {
            pos++;
            file.seek(pos);
            resultLine.append(file.read(1));
            if (file.read(DELIM.size()) != DELIM)
            {
                continue;
            }

            data = resultLine.toUtf8();

            QList<QByteArray> list =
                Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
            if (list.size() != 9)
            {
                qDebug() << "[Error][StoredIndex][getStoredByPath] List!=9";
                file.close();
                return qlist_st;
            }
            if (list.at(7) == _path)
            {
                QList<QByteArray> l;
                for (int j = 0; j < 9; j++)
                    l << list.at(j);

                QByteArray serialized =
                    Serialization::serialize(l, Serialization::INFORMATION_SEPARATOR_ONE);
                qlist_st.append(Stored(serialized));
            }
            data.clear();
            resultLine.clear();
            pos += DELIM.size();

            //  QList<QByteArray> list =
            //    Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
            // if (list.at(7) == _hash)
            //            {
            //                QList<QByteArray> l;
            //                for (int j = 0; j < 12; j++)
            //                    l << list.at(j);

            //                QByteArray serialized =
            //                    Serialization::serialize(l,
            //                    Serialization::INFORMATION_SEPARATOR_ONE);
            //                qlist_st.append(Stored(serialized));
            //            }
            //            else
            //                continue;
        }
        file.close();
        return qlist_st;
    }

    qDebug() << "[Error][StoredIndex][getStoredByPath] File not open" << _path + ".strd";
    return qlist_st;
}
QByteArray StoredIndex::calcChangeSig(QByteArray _changeData)
{
    return account_contrlr->getActor(account_contrlr->getUserNum())
        .getKey()
        ->sign(_changeData);
}

Stored StoredIndex::getLastStoredByPath(QByteArray _path) const
{

    QFile file(_path + ".strd");
    QByteArray data;
    if (!file.exists())
    {
        qDebug() << "Can't get the file" << _path << "(File is not exist)";
        return Stored();
    }
    if (file.open(QIODevice::ReadOnly))
    {
        int k = DELIM.size();
        QString result = "";
        while (file.size() - k > 0)
        {
            file.seek(file.size() - k);
            QString test = file.read(DELIM.size());
            if (test != DELIM)
                k++;
            else
            {

                while (file.size() - k > 0)
                {
                    k++;
                    file.seek(file.size() - k);
                    result.push_front(file.read(1));

                    QList<QByteArray> l;

                    if (file.size() - k + DELIM.size() > 0)
                    {
                        file.seek(file.size() - k);
                        QString test = file.read(DELIM.size());
                        if (test == DELIM)
                        {
                            result.remove(0, DELIM.size());
                            QList<QByteArray> list = Serialization::deserialize(
                                result.toUtf8(), Serialization::USER_FIELD_SPLITER);
                            //  if (list.at(5) == _path)
                            //  {
                            QList<QByteArray> l;
                            for (int j = 0; j < 9; j++)
                                l << list.at(j);

                            QByteArray serialized = Serialization::serialize(
                                l, Serialization::INFORMATION_SEPARATOR_ONE);
                            file.close();
                            return Stored(serialized);
                        }
                    }
                }
                QList<QByteArray> list =
                    Serialization::deserialize(data, Serialization::USER_FIELD_SPLITER);
                //  if (list.at(5) == _path)
                //  {
                if (list.size() != 9)
                {
                    qDebug() << "[Error][StoredIndex][getLastStoredByPath]";
                    file.close();
                    return Stored();
                }
                QList<QByteArray> l;
                for (int j = 0; j < 9; j++)
                    l << list.at(j);

                QByteArray serialized =
                    Serialization::serialize(l, Serialization::INFORMATION_SEPARATOR_ONE);
                file.close();
                return Stored(serialized);
            }
        }
        qDebug() << "[Error][StoredIndex][getLastStoredByPath] Unknown Error";
        return Stored();
    }

    qDebug() << "[Error][StoredIndex][getLastStoredByPath]" << _path << " didn't open.";
    return Stored();
}
void StoredIndex::addStoredInIndex(Stored getStored)
{
    if (validateStored(getStored))
    {
        getStored.setChangeDataSig(calcChangeSig(getStored.getChangeData()));

        addStored(getStored);
    }
    else
        qDebug() << "Stored with actor id='" << getStored.getAuthor()
                 << "' is not valid. Error add stored";
}

Stored StoredIndex::addSerializedStoredInIndex(QByteArray serialized)
{
    QList<QByteArray> qlist = Serialization::desirializeStored(serialized);
    if (qlist.size() != 2)
    {
        qDebug() << "Error in method addSerializedStoredInIndex, StoredIndex.";
    }
    QList<QByteArray> headerList =
        Serialization::deserialize(qlist[0], Serialization::DFS_STORED_DELIMETR);
    QByteArray actorSign = Utils::calcKeccak(
        qlist[1]); /*this->account_contrlr->getActor(BigNumber(headerList[0]))
.getKey()
->sign(qlist[1]);*/
    QByteArray getPrevSig = Utils::calcKeccak(
        qlist[1]); /*this->getLastStoredByPath(headerList[3]).getChangeDataSig();*/

    //!!!!!!!!!!!!!!!!!!!!
    QByteArray getPrevStoredHash = this->getLastStoredByPath(headerList[3]).getHash();
    //    refactoring Assert List out of range

    if (headerList.size() != 5)
    {
        qDebug() << "Error in method addSerializedStoredInIndex, Header List don't have "
                    "4 parametres.";
    }

    // actor id, first byte, state, path
    Stored newStored(BigNumber(headerList[0]), headerList[1].toInt(), qlist[1], actorSign,
                     headerList[3], getPrevSig, "getPrevStoredHash",
                     storedSpace::convertToDFSstate(headerList[2]));
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
