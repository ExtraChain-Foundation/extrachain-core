/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "managers/chat.h"
#include "managers/chatmanager.h"

Chat::Chat(ChatManager* chatManager, QByteArray chatId, ActorIndex* actorIndex,
           AccountController* accountController, BigNumber sessionNumb)
{
    this->_chatManager = chatManager;
    this->_chatId = chatId;
    this->_currentActorId = accountController->getMainActor()->id().toActorId();
    this->_actorIndex = actorIndex;
    this->_accountController = accountController;
    this->_encryptionKey = unloadChatKey();
    if (sessionNumb != -1)
        this->_currentSession = sessionNumb;
    else
        this->_currentSession = getActualCurrentSession();

    InitializeAllPaths();
}

Chat::Chat(ChatManager* chatManager, QByteArray chatId, QByteArray key, BigNumber currentSession,
           ActorIndex* actorIndex, AccountController* accountController, QList<QByteArray> users,
           QByteArray _ownerId)
{
    this->_chatManager = chatManager;
    this->_chatId = chatId;
    this->_encryptionKey = key;
    this->_accountController = accountController;
    this->_currentActorId = accountController->getMainActor()->id().toActorId();
    //    if (currentSession == 0)
    //        this->_currentSession = getActualCurrentSession();
    //    else
    this->_currentSession = currentSession;
    this->_actorIndex = actorIndex;
    this->ownerID = _ownerId;
    InitializeAllPaths();
    createNewSession(key, users, _ownerId);
}

Chat::Chat(const Chat& tempChat)
{
    this->_chatManager = tempChat._chatManager;
    this->_chatId = tempChat.getChatId();
    this->_encryptionKey = tempChat.getEncryptionKey();
    this->_currentSession = tempChat.getSessionConst();
    this->_accountController = tempChat.getAccountController();
    this->_currentActorId = tempChat.getCurrentActorId();
    this->_actorIndex = tempChat.getActorIndex();
    InitializeAllPaths();
}

bool Chat::isOwner()
{
    //    return QFile(_actorPath + _currentActorId + "/chatStorage/" + _chatId + "/users/" + _currentActorId)
    //        .exists();
    return true;
}

bool Chat::isUserActual(QByteArray actorId, BigNumber sessionNumb)
{
    //    QFile file(_actorPath + actorId + "/myChats/" + _chatId + "/currentSession");
    //    if (!file.exists())
    //        return false;
    //    if (file.open(QIODevice::ReadOnly))
    //    {
    //        QByteArray actorSession = file.readLine();
    //        file.close();
    //        return sessionNumb == actorSession;
    //    }
    //    else
    //    {
    //        qDebug() << "[Warning] Cann't open file on read. isUserActual in Chat. path="
    //                 << _actorPath + actorId + "/myChats/" + _chatId + "/currentSession";
    //        return false;
    //    }
    return true;
}

void Chat::saveChatKey(QByteArray key, BigNumber sessionNumb, QByteArray& _ownerId)
{
    if (this->ownerID == "-1")
    {
        // _ownerId = _currentActorId;
        this->ownerID = _ownerId;
    }

    auto mainActor = _accountController->getMainActor()->key();
    _chatManager->sendEditSql(_currentActorId, "chats", DfsStruct::Type::Private, DfsStruct::Insert,
                              { Config::DataStorage::chatIdTableName.c_str(), "chatId",
                                mainActor->encryptSymmetric(_chatId), "key", mainActor->encryptSymmetric(key),
                                "owner", mainActor->encryptSymmetric(_ownerId) });

    saveChatsId(_chatId);
}

void Chat::saveChatsId(const QByteArray& chatId)
{ // TODO: maybe remove?
    return;
    if (chatId.size() < 63)
    {
        qDebug() << "Kurnul?";
        return;
    }
    QByteArray pathR = "keystore/chats/";
    pathR += _currentActorId + "/";
    QDir().mkpath(pathR);
    if (QFile().exists(pathR + "fileChatsId"))
    {
        QFile file(pathR + "fileChatsId");
        file.open(QIODevice::ReadWrite);
        QByteArray dataFromFile = file.readAll();
        QByteArrayList listChats = Serialization::deserialize(dataFromFile, 4);
        if (!listChats.contains(chatId))
        {
            listChats.append(chatId);
        }
        else
        {
            file.close();
        }
        file.resize(0);
        file.write(Serialization::serialize(listChats, 4));
        file.close();
    }
    else
    {
        QFile file(pathR + "fileChatsId");
        file.open(QIODevice::ReadWrite);
        QByteArray listChats = Serialization::serialize({ chatId }, 4);
        file.write(listChats);
        file.close();
    }
}

BigNumber Chat::getSessionConst() const
{
    return _currentSession;
}

QByteArray Chat::unloadChatKey()
{
    if (_encryptionKey != "0")
        return _encryptionKey;
    QString filePath = DfsStruct::ROOT_FOOLDER_NAME + "/" + _currentActorId + "/private/chats";

    if (!QFile::exists(filePath))
        return "";

    auto mainActor = _accountController->getMainActor()->key();
    DBConnector DB(filePath.toStdString());
    std::vector<DBRow> res =
        DB.select("SELECT * FROM " + Config::DataStorage::chatIdTableName + " WHERE chatId = ?",
                  Config::DataStorage::chatIdTableName,
                  { { "chatId", mainActor->encryptSymmetric(_chatId).toStdString() } });
    if (res.size() == 0)
    {
        qDebug() << "[Error] Chat manager can't open file to load the key";
        return "0";
    }

    _encryptionKey = mainActor->decryptSymmetric(QByteArray::fromStdString(res[0]["key"]));
    this->ownerID = mainActor->decryptSymmetric(QByteArray::fromStdString(res[0]["owner"]));
    return _encryptionKey;
    //    if (!file.exists())
    //        return "0";
    //    if (file.open(QIODevice::ReadOnly))
    //    {
    //        QByteArray key = file.readLine();
    //        file.close();
    //        return key;
    //    }
}

// BigNumber Chat::getMyCurrentSession()
//{
//    if (this->_currentSession != -1)
//        return this->_currentSession;
//    QFile file(getPathCurrentChat() + "currentSession");
//    if (!file.exists())
//    {
//        BigNumber currentSession = 0;
//        if (file.open(QIODevice::WriteOnly))
//        {
//            currentSession = findCurrentSession();
//            file.write(currentSession.toByteArray());
//            file.close();
//            this->_currentSession = currentSession;
//            emit sendDataToBlockchain(getPathCurrentChat() + "currentSession");
//            return currentSession;
//        }
//        else
//            qDebug() << "[Warning] cannot open file to write session in chat manager";
//        return -1;
//    }

//    if (file.open(QIODevice::ReadOnly))
//    {
//        this->_currentSession = BigNumber(file.readLine());
//        file.close();
//        return this->_currentSession;
//    }
//    qDebug() << "[Error] Chat manager can't open file to load the key";
//    return -1;
//}

QByteArray Chat::getCurrentActorId() const
{
    return _currentActorId;
}

// QByteArray Chat::getChatPath() const
//{
//    return _chatPath;
//}

QList<QByteArray> Chat::getAllUsers()
{
    QList<QByteArray> result;
    QString pathToUsers = DfsStruct::ROOT_FOOLDER_NAME + "/" + ownerID + "/chats/" + _chatId + "/users";

    if (!QFile::exists(pathToUsers))
    {
        if (ownerID == _currentActorId)
            return {};
        result << _currentActorId << ownerID;
        return result;
    }

    DBConnector DB(pathToUsers.toStdString());
    std::vector<DBRow> res = DB.select("SELECT * FROM " + Config::DataStorage::chatUserTableName);
    if (res.size() < 2)
    {
        qDebug() << "UsersChatIsEmpty";
        if (ownerID == _currentActorId)
            return {};
        result << _currentActorId << ownerID;
        return result;
    }
    for (DBRow tmp : res)
    {
        result.append(tmp["userId"].c_str());
    }
    return result;
}

QList<UIMessage> Chat::getAllMessages()
{
    QList<UIMessage> result;
    QString dbPath = DfsStruct::ROOT_FOOLDER_NAME + "/" + ownerID + "/chats/" + _chatId + "/"
        + _currentSession.toByteArray() + "/msg";

    if (!QFile::exists(dbPath))
        return {};

    DBConnector DB(dbPath.toStdString());

    std::vector<DBRow> row;
    row = DB.select("SELECT * FROM " + Config::DataStorage::chatMessageTableName);
    if (row.size() == 0)
    {
        qDebug() << "Haven`t chat";
    }
    for (DBRow tmp : row)
    {
        UIMessage ui;
        ui.messId = decryptMessage(QByteArray::fromStdString(tmp["messId"]));
        ui.userId = decryptMessage(QByteArray::fromStdString(tmp["userId"]));
        ui.message = decryptMessage(QByteArray::fromStdString(tmp["message"]));
        QByteArray date = QByteArray::fromStdString(tmp["date"]);
        ui.date = QDateTime::fromMSecsSinceEpoch(date.toLongLong());
        result.append(ui);
    }

    return result;
}

ActorIndex* Chat::getActorIndex() const
{
    return _actorIndex;
}

QByteArray Chat::getOwner()
{
    return ownerID;
}

QByteArray Chat::encryptByChatKey(QByteArray data)
{
    return encryptMessage(data);
}

QByteArray Chat::decryptByChatKey(QByteArray data)
{
    return decryptMessage(data);
}

UIMessage Chat::getLastMessage()
{
    UIMessage message;
    if (!QFile::exists(DfsStruct::ROOT_FOOLDER_NAME + "/" + ownerID + "/chats/" + _chatId + "/"
                       + _currentSession.toByteArray() + "/msg"))
    {
        qDebug() << "[Error] File with session doesn't open. getAllMessagesByteArray Chat";
        return {};
    }

    DBConnector DB(DfsStruct::ROOT_FOOLDER_NAME.toStdString() + "/" + ownerID.toStdString() + "/chats/"
                   + _chatId.toStdString() + "/" + _currentSession.toStdString() + "/msg");

    std::vector<DBRow> row;
    row = DB.select("SELECT * FROM " + Config::DataStorage::chatMessageTableName
                    + " ORDER BY date DESC LIMIT 1");
    if (row.size() == 0)
    {
        return {};
    }
    message.userId = decryptMessage(QByteArray::fromStdString(row[0]["userId"]));
    message.messId = decryptMessage(QByteArray::fromStdString(row[0]["messId"]));
    if (row[0]["message"].size() == 0)
        message.message = "";
    else
        message.message = decryptMessage(QByteArray::fromStdString(row[0]["message"]));
    QByteArray date = row[0]["date"].c_str();
    message.date = QDateTime::fromMSecsSinceEpoch(date.toLongLong());
    return message;
    //    QFile file(pathToSession(_currentSession) + "/session");
    //    if (file.open(QIODevice::ReadOnly))
    //    {

    //        list = Serialization::deserialize(file.readAll());
    //        file.close();
    //        return list;
    //    }

    return {};
}

void Chat::removeAllChatData()
{
    QDir(getPathCurrentChat()).removeRecursively();
}

QString Chat::getPathCurrentChat()
{
    return DfsStruct::ROOT_FOOLDER_NAME + "/" + ownerID + "/chats/" + _chatId + "/";
}

// QByteArray Chat::getPathMyChatsKeyStore()
//{
//    return getPathMyChatsCurrentChat() + "keystore/";
//}

QString Chat::getPathToUsers()
{
    return DfsStruct::ROOT_FOOLDER_NAME + "/" + ownerID + "/chats/" + _chatId + "/"
        + _currentSession.toByteArray() + "/";
}

BigNumber Chat::findCurrentSession()
{
    BigNumber currentSession("-1");
    QStringList allSessions = QDir(getPathCurrentChat()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (QString temp : allSessions)
    {
        if (BigNumber(temp.toUtf8()) > currentSession)
            currentSession = BigNumber(temp.toUtf8());
    }
    if (currentSession == -1)
    {
        if (ownerID != "-1" && _chatId != "0")
            _currentSession = BigNumber("0");
        qDebug() << "[Warning] Chat. find Current Session. There no any session in file.";
    }
    return currentSession;
    //    QFile file;
    //    do
    //    {
    //        currentSession++;
    //        file.setFileName(getPathToSessions() + currentSession.toByteArray());
    //    } while (file.exists());
    //    currentSession--;
}

void Chat::InitializeAllPaths()
{
    if (_currentSession != -1 && ownerID != "-1")
        QDir().mkpath(getPathToUsers());

    //    QDir().mkpath(pathToSession(_currentSession));
}

// void Chat::InitializeOwnerPathNewChat()
//{
//    QDir().mkpath(getPathToUsers());
//    //    QDir().mkpath(getPathToSessions());
//    //    QDir().mkpath(getPathMyChatsKeyStore());
//}
bool Chat::createNewSession(QByteArray key, QList<QByteArray> users, QByteArray _ownerId)
{
    if (users.empty())
    {
        qDebug() << "[Error] Chat. createNewSession, users list is empty, it's wrong";
        return false;
    }
    saveChatKey(key, this->_currentSession, _ownerId);
    if (!isUserExist(ownerID, users))
        users.append(ownerID);
    createNewUsersDb(users);
    //    QFile data(pathToSession(this->_currentSession) + "/session");
    //    data.open(QIODevice::WriteOnly);
    //    data.flush();
    //    data.close();

    DBConnector DB(DfsStruct::ROOT_FOOLDER_NAME.toStdString() + "/" + _ownerId.toStdString() + "/chats/"
                   + _chatId.toStdString() + "/" + _currentSession.toStdString() + "/msg");
    DB.createTable(Config::DataStorage::sessionChatMessageStorage);
    return true;
    //    QFile file(getPathToSessions() + "0");
    //    file.open(QIODevice::WriteOnly);
    //    file.close();
    //    emit sendDataToBlockchain(getPathToSessions() + "0"); // creation 0 session
    //    if (ownerId != "-1")
    //    {
    //        file.setFileName(getPathToUsers() + ownerId);
    //        if (file.open(QIODevice::WriteOnly))
    //        {
    //            file.write("owner");
    //            file.close();
    //            emit sendDataToBlockchain(getPathToUsers() + ownerId);
    //        }
    //        else
    //            qDebug() << "[Error] Cannot create Chat owner";
    //    }
    //    if (prevSessionNumber < 0)
    //    {
    //        if (_currentActorId != ownerId)
    //            users = { ownerId, _currentActorId };
    //        else
    //            users = { _currentActorId };
    //    }
}
// QByteArray Chat::getChatPrivateKey()
//{
//    return _accountController->getMainActor()->getKey()->decrypt(unloadChatKey());
//}

BigNumber Chat::getActualCurrentSession()
{
    return findCurrentSession();
}
QByteArray Chat::encryptMessage(QByteArray message)
{
    return BlowFish::encrypt(message, unloadChatKey());
}
QByteArray Chat::decryptMessage(QByteArray message)
{
    return BlowFish::decrypt(message, unloadChatKey());
}

void Chat::createNewUsersDb(QList<QByteArray> userList, QList<QByteArray> userData)
{
    DBConnector DB(DfsStruct::ROOT_FOOLDER_NAME.toStdString() + "/" + userList[0].toStdString() + "/chats/"
                   + _chatId.toStdString() + "/users");
    DB.createTable(Config::DataStorage::chatUserStorage);

    for (QByteArray user : userList)
    {
        DBRow row;
        row.insert({ "userId", user.toStdString() });
        DB.insert(Config::DataStorage::chatUserTableName, row);
    }
}

bool Chat::isUserExist(QByteArray actorId, QList<QByteArray> userList)
{
    for (QByteArray user : userList)

        if (user == actorId)
            return true;

    return false;
}

QByteArray Chat::sendMessage(QByteArray message)
{
    //    DataBase
    if (_currentSession != BigNumber("0"))
        _currentSession = BigNumber("0");
    DBConnector DB(DfsStruct::ROOT_FOOLDER_NAME.toStdString() + "/" + ownerID.toStdString() + "/chats/"
                   + _chatId.toStdString() + "/" + _currentSession.toStdString() + "/msg");

    DBRow row;
    qint64 messId = QDateTime::currentMSecsSinceEpoch() + QRandomGenerator::global()->bounded(100);
    row.insert({ "messId", encryptMessage(QByteArray::number(messId)).toStdString() });
    row.insert({ "userId", encryptMessage(_currentActorId).toStdString() });
    row.insert({ "message", encryptMessage(message).toStdString() });
    row.insert({ "type", encryptMessage("msg").toStdString() });
    row.insert({ "session", encryptMessage(_currentSession.toByteArray()).toStdString() });
    row.insert({ "date", QByteArray::number(QDateTime::currentMSecsSinceEpoch()).toStdString() });
    DB.insert(Config::DataStorage::chatMessageTableName, row);
    // return currentMessageByteArray;

    return "";
}

QByteArray Chat::getChatId() const
{
    return this->_chatId;
}

QByteArray Chat::getEncryptionKey() const
{
    return this->_encryptionKey;
}

BigNumber Chat::getSession()
{
    if (this->_currentSession != 0) // temp
        this->_currentSession = BigNumber(0);
    return this->_currentSession;
}

AccountController* Chat::getAccountController() const
{
    return this->_accountController;
}

void Chat::InviteNewUser(QByteArray actorId)
{
    QList<QByteArray> users = getAllUsers();
    if (!isUserExist(actorId, users))
    {
        DBConnector DB(DfsStruct::ROOT_FOOLDER_NAME.toStdString() + "/" + ownerID.toStdString() + "/chats/"
                       + _chatId.toStdString() + "/users");
        DBRow row;
        row.insert({ "userId", actorId.toStdString() });
        DB.insert(Config::DataStorage::chatUserTableName, row);

        users.append((actorId));
        //        loadUsers(users);
    }
    return;
    //    QDir().mkpath(_actorPath + actorId + "/myChats/" + _chatId + "/");
    //    QFile file(_actorPath + actorId + "/myChats/" + _chatId + "/" + _chatId + ".dat");
    //    if (file.open(QIODevice::WriteOnly))
    //    {
    //        //        file.write(_chatPath);
    //        file.close();
    //        emit sendDataToBlockchain(_actorPath + actorId + "/myChats/" + _chatId + "/" + _chatId +
    //        ".dat");
    //    }
    //    else
    //    {
    //        qDebug() << "[Warning] Error open file on write when invite new user "
    //                 << _actorPath + actorId + "/myChats/" + _chatId + "/" + _chatId + ".dat";
    //    }
    //    file.setFileName(_actorPath + actorId + "/myChats/" + _chatId + "/currentSession");
    //    if (file.open(QIODevice::WriteOnly))
    //    {
    //        file.write(getMyCurrentSession());
    //        file.close();
    //        emit sendDataToBlockchain(_actorPath + actorId + "/myChats/" + _chatId + "/currentSession");
    //    }
    //    else
    //    {
    //        qDebug() << "[Warning] Error open file on write when invite new user "
    //                 << _actorPath + actorId + "/myChats/" + _chatId + "/currentSession";
    //    }
    //    QDir().mkpath(_actorPath + actorId + "/myChats/" + _chatId + "/keystore/");
    //    file.setFileName(_actorPath + actorId + "/myChats/" + _chatId + "/keystore/key" +
    //    getMyCurrentSession()); if (file.open(QIODevice::WriteOnly))
    //    {
    //        file.write(inviterSign);
    //        file.close();
    //        emit sendDataToBlockchain(_actorPath + actorId + "/myChats/" + _chatId + "/keystore/key"
    //                                  + getMyCurrentSession());
    //    }
    //    else
    //    {
    //        qDebug() << "[Warning] Error open file on write when invite new user "
    //                 << _actorPath + actorId + "/myChats/" + _chatId + "/keystore/key" +
    //                 getMyCurrentSession();
    //    }

    //    QList<QByteArray> signData;
    //    signData.append(_currentActorId);
    //    signData.append(inviterSign);
    //    file.setFileName(getPathToUsers() + actorId);
    //    if (file.open(QIODevice::WriteOnly))
    //    {
    //        file.write(Serialization::serialize(signData));
    //        file.close();
    //        emit sendDataToBlockchain(getPathToUsers() + actorId);
    //        return;
    //    }
    //    else
    //        qDebug() << "[Error] when try to write data about new user";
}
bool Chat::isUserVerify(QByteArray actorId) // CYCLE instead of recursive?!?!?!?!?!?!?!?!
{
    //    QFile file(getPathToUsers() + actorId);
    //    if (!file.exists())
    //        return false;
    //    if (file.open(QIODevice::ReadOnly))
    //    {
    //        QByteArray data = file.readLine();
    //        if (data == "owner" || data == "owner\n")
    //            return true;
    //        QList<QByteArray> list = Serialization::deserialize(data);
    //        if (list.size() != 2)
    //            return false;
    //        if (!_actorIndex->getActor(BigNumber(list.at(0))).getKey()->verify(list.at(0), list.at(1)))
    //            return false;
    //        return isUserVerify(list.at(0));
    //    }
    //    qDebug() << "[Error] Cannot open file for user verify in chat manager.";
    //    return false;
    return true;
}

QString Chat::pathToSession(BigNumber sessionNumber)
{
    return DfsStruct::ROOT_FOOLDER_NAME + "/" + ownerID + "/" + _chatId + "/" + sessionNumber.toByteArray()
        + "/";
}

Chat::~Chat()
{
}
