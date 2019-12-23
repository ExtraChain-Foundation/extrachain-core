#include "managers/chat.h"

Chat::Chat(QByteArray chatId, ActorIndex* actorIndex, AccountController* accountController,
           BigNumber sessionNumb)
{
    this->_chatId = chatId;
    this->_currentActorId = accountController->getMainActor()->getId().toActorId();
    this->_actorIndex = actorIndex;
    this->_accountController = accountController;
    if (sessionNumb != -1)
        this->_currentSession = sessionNumb;
    else
        this->_currentSession = getActualCurrentSession();
    this->_encryptionKey = unloadChatKey();
    InitializeAllPaths();
}

Chat::Chat(QByteArray chatId, QByteArray key, BigNumber currentSession, ActorIndex* actorIndex,
           AccountController* accountController, QList<QByteArray> users, QByteArray ownerId)
{

    this->_chatId = chatId;
    this->_encryptionKey = key;
    this->_accountController = accountController;
    this->_currentActorId = accountController->getMainActor()->getId().toActorId();
    //    if (currentSession == 0)
    //        this->_currentSession = getActualCurrentSession();
    //    else
    this->_currentSession = currentSession;
    this->_actorIndex = actorIndex;
    InitializeAllPaths();
    createNewSession(key, users, ownerId);
}

Chat::Chat(const Chat& tempChat)
{
    this->_chatId = tempChat.getChatId();
    this->_encryptionKey = tempChat.getEncryptionKey();
    this->_currentSession = tempChat.getSession();
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
void Chat::saveChatKey(QByteArray key, BigNumber sessionNumb, QByteArray ownerId)
{

    if (ownerId == "-1")
        ownerId = _currentActorId;
    QDir().mkpath(ChatStorage::KEYSTORE_CHATS.c_str());
    DBConnector DB(ChatStorage::KEYSTORE_CHATS + "chatsId");
    DB.createTable(Config::DataStorage::chatIdStorage);

    DBRow row;
    row.insert({ "chatId", _chatId.toStdString() });
    row.insert({ "key", key.toStdString() });
    row.insert({ "owner", ownerId.toStdString() });
    DB.insert(Config::DataStorage::chatIdTableName, row);
}
QByteArray Chat::unloadChatKey()
{
    QDir().mkpath(ChatStorage::KEYSTORE_CHATS.c_str());
    DBConnector DB(ChatStorage::KEYSTORE_CHATS + "chatsId");
    std::vector<DBRow> res = DB.select("SELECT * FROM " + Config::DataStorage::chatIdTableName
                                       + " WHERE chatId = " + "'" + _chatId.toStdString() + "';");
    if (res.size() == 0)
    {
        qDebug() << "[Error] Chat manager can't open file to load the key";
        return "0";
    }
    QByteArray key = res[0]["key"].c_str();
    this->ownerID = res[0]["owner"].c_str();
    return key;
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
    QByteArray pathToUsers = ChatStorage::STORED_CHATS + _currentActorId + "/chats/" + _chatId + "/users";
    DBConnector DB(pathToUsers.toStdString());
    DB.createTable(Config::DataStorage::chatUserStorage);
    std::vector<DBRow> res = DB.select("SELECT * FROM " + Config::DataStorage::chatUserTableName);
    if (res.size() == 0)
    {
        qDebug() << "UsersChatIsEmpty";
        return QByteArrayList();
    }
    QList<QByteArray> result;
    for (DBRow tmp : res)
    {
        result.append(tmp["userId"].c_str());
    }
    return result;
}

QList<UIMessage> Chat::getAllMessages()
{
    QList<UIMessage> result;

    DBConnector DB(ChatStorage::STORED_CHATS.toStdString() + ownerID.toStdString() + "/chats/"
                   + _chatId.toStdString() + "/" + _currentSession.toStdString() + "/msg");
    if (DB.createTable(Config::DataStorage::sessionChatMessageStorage))
    {
        std::vector<DBRow> row;
        row = DB.select("SELECT * FROM " + Config::DataStorage::chatMessageTableName);
        if (row.size() == 0)
        {
            qDebug() << "Haven`t chat";
        }
        for (DBRow tmp : row)
        {
            UIMessage ui;
            ui.userId = tmp["userId"].c_str();
            ui.message = decryptMessage(tmp["message"].c_str());
            QByteArray date = tmp["date"].c_str();
            ui.date = QDateTime::fromMSecsSinceEpoch(date.toLongLong());
            result.append(ui);
        }
    }
    return result;
    //        message.userId = decryptMessage(row[0]["userId"].c_str());
    //        message.message = decryptMessage(row[1]["message"].c_str());
    //        QByteArray date = decryptMessage(row[4]["date"].c_str());
    //        message.date = QDateTime::fromMSecsSinceEpoch(date.toLongLong());
    //        return message;
    //    QList<QByteArray> allMessages;
    //    if (allMessages.empty())
    //        return {};

    //    QList<QByteArray> currentData;

    //    QByteArray decryptedCurrentMessage;
    //    for (QByteArray msginList : allMessages)
    //    {
    //        decryptedCurrentMessage = decryptMessage(msginList);
    //        currentData = Serialization::universalDeserialize(decryptedCurrentMessage);
    //        if (currentData.size() == 3)
    //        {
    //            result.append(UIMessage{ currentData.at(0), currentData.at(1),
    //                                     QDateTime::fromMSecsSinceEpoch(currentData.at(2).toLongLong()) });
    //        }
    //        else
    //        {
    //            qDebug() << "[Error] Size !=3 in getAllMessages Chat";
    //        }
    //    }
    //    return result;
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
    DBConnector DB(ChatStorage::STORED_CHATS.toStdString() + ownerID.toStdString() + "/chats/"
                   + _chatId.toStdString() + "/" + _currentSession.toStdString() + "/msg");
    if (DB.createTable(Config::DataStorage::sessionChatMessageStorage))
    {
        std::vector<DBRow> row;
        row = DB.select("SELECT * FROM " + Config::DataStorage::chatMessageTableName
                        + " ORDER BY date DESCLIMIT 1");
        if (row.size() == 0)
        {
            qDebug() << "[Error] File with session doesn't open. Chat";
            return {};
        }
        message.userId = row[0]["userId"].c_str();
        if (row[0]["message"].size() == 0)
            message.message = "";
        else
            message.message = decryptMessage(row[0]["message"].c_str());
        QByteArray date = row[0]["date"].c_str();
        message.date = QDateTime::fromMSecsSinceEpoch(date.toLongLong());
        return message;
    }
    else
        qDebug() << "[Error] File with session doesn't open. getAllMessagesByteArray Chat";
    //    QFile file(pathToSession(_currentSession) + "/session");
    //    if (file.open(QIODevice::ReadOnly))
    //    {

    //        list = Serialization::universalDeserialize(file.readAll());
    //        file.close();
    //        return list;
    //    }

    return {};
}

void Chat::removeAllChatData()
{
    QDir(getPathCurrentChat()).removeRecursively();
}

QByteArray Chat::getPathCurrentChat()
{
    return ChatStorage::STORED_CHATS + _currentActorId + "/chats/" + _chatId + "/";
}

// QByteArray Chat::getPathMyChatsKeyStore()
//{
//    return getPathMyChatsCurrentChat() + "keystore/";
//}

QByteArray Chat::getPathToUsers()
{
    return ChatStorage::STORED_CHATS + _currentActorId + "/chats/" + _chatId + "/"
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
        qDebug() << "[Warning] Chat. find Current Session. There no any session in file.";
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
    if (_currentSession != -1)
        QDir().mkpath(getPathToUsers());

    //    QDir().mkpath(pathToSession(_currentSession));
}

// void Chat::InitializeOwnerPathNewChat()
//{
//    QDir().mkpath(getPathToUsers());
//    //    QDir().mkpath(getPathToSessions());
//    //    QDir().mkpath(getPathMyChatsKeyStore());
//}
bool Chat::createNewSession(QByteArray key, QList<QByteArray> users, QByteArray ownerId)
{
    if (users.empty())
    {
        qDebug() << "[Error] Chat. createNewSession, users list is empty, it's wrong";
        return false;
    }
    saveChatKey(key, this->_currentSession, ownerId);
    if (!isUserExist(_currentActorId, users))
        users.append(_currentActorId);
    loadUsers(users);
    //    QFile data(pathToSession(this->_currentSession) + "/session");
    //    data.open(QIODevice::WriteOnly);
    //    data.flush();
    //    data.close();

    DBConnector DB(ChatStorage::STORED_CHATS.toStdString() + ownerID.toStdString() + "/chats/"
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
    return blowFish_crypt().EncryptBlowFish(message, unloadChatKey());
}
QByteArray Chat::decryptMessage(QByteArray message)
{
    return blowFish_crypt().DecryptBlowFish(message, unloadChatKey());
}

void Chat::loadUsers(QList<QByteArray> userList, QList<QByteArray> userData)
{
    DBConnector DB(ChatStorage::STORED_CHATS.toStdString() + _currentActorId.toStdString() + "/chats/"
                   + _chatId.toStdString() + "/users");
    DB.createTable(Config::DataStorage::chatUserStorage);

    for (QByteArray user : userList)
    {
        //        QFile userfile(getPathToUsers() + user);
        //        userfile.open(QIODevice::WriteOnly);
        //        userfile.flush();
        //        userfile.close();
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
    DBConnector DB(ChatStorage::STORED_CHATS.toStdString() + _currentActorId.toStdString() + "/chats/"
                   + _chatId.toStdString() + "/" + _currentSession.toStdString() + "/msg");

    if (DB.createTable(Config::DataStorage::sessionChatMessageStorage))
    {
        DBRow row;
        row.insert({ "userId", _currentActorId.toStdString() });
        row.insert({ "message", encryptMessage(message).toStdString() });
        row.insert({ "type", "blob" });
        row.insert({ "session", _currentSession.toByteArray().toStdString() });
        row.insert({ "date", QByteArray::number(QDateTime::currentMSecsSinceEpoch()).toStdString() });
        DB.insert(Config::DataStorage::chatMessageTableName, row);
        //        return currentMessageByteArray;
    }
    else
        qDebug() << "[Warning] Cannot open file with session to send message. SendMessage, Chat";

    return "";
}

void Chat::receiveMessage(QByteArray message)
{
    DBConnector DB(ChatStorage::STORED_CHATS.toStdString() + _currentActorId.toStdString() + "/chats/"
                   + _chatId.toStdString() + "/" + _currentSession.toStdString() + "/msg");
    if (DB.createTable(Config::DataStorage::sessionChatMessageStorage))
    {
        DBRow row;
        row.insert({ "userId", _currentActorId.toStdString() });
        row.insert({ "message", encryptMessage(message).toStdString() });
        row.insert({ "type", "blob" });
        row.insert({ "session", _currentSession.toByteArray().toStdString() });
        row.insert({ "date", QByteArray::number(QDateTime::currentMSecsSinceEpoch()).toStdString() });
        DB.insert(Config::DataStorage::chatMessageTableName, row);
    }
    //    QFile file(pathToSession(_currentSession));
    //    if (file.open(QIODevice::Append))
    //    {

    //        file.write(Serialization::universalSerialize({ message }));

    //        file.close();

    //        //   emit sendDataToBlockchain(getPathToSessions() + getMyCurrentSession());
    //    }
    //    else

    //        qDebug()
    //        << "[Warning] Cannot open file with session to send message. receiveMessage, Chat";
    //    QFile file(pathToSession(_currentSession) + "/session");
    //    if (file.open(QIODevice::Append))
    //    {

    //        message = encryptMessage(message) + "\n";
    //        file.write(message);

    //        file.close();
    //        //   emit sendDataToBlockchain(getPathToSessions() + getMyCurrentSession());
    //    }
    //    else

    //        qDebug() << "[Warning] Cannot open file with session to send message. SendMessage, Chat";
}

QByteArray Chat::getChatId() const
{
    return this->_chatId;
}

QByteArray Chat::getEncryptionKey() const
{
    return this->_encryptionKey;
}

BigNumber Chat::getSession() const
{
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
        DBConnector DB(ChatStorage::STORED_CHATS.toStdString() + _currentActorId.toStdString() + "/chats/"
                       + _chatId.toStdString() + "/users");
        DB.createTable(Config::DataStorage::chatUserStorage);
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
    //        file.write(Serialization::universalSerialize(signData));
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
    //        QList<QByteArray> list = Serialization::universalDeserialize(data);
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

QByteArray Chat::pathToSession(BigNumber sessionNumber)
{
    return ChatStorage::STORED_CHATS + _currentActorId + "/" + _chatId + "/" + sessionNumber.toByteArray()
        + "/";
}

Chat::~Chat()
{
}
