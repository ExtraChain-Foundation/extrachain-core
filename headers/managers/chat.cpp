#include "chat.h"

Chat::Chat(QByteArray chatId, ActorIndex* actorIndex, AccountController* accountController,
           BigNumber sessionNumb)
{

    this->_chatId = chatId;

    this->_accountController = accountController;
    if (sessionNumb != -1)
        this->_currentSession = sessionNumb;
    else
        this->_currentSession = getActualCurrentSession();
    this->_encryptionKey = unloadChatKey();
    this->_currentActorId = accountController->getMainActor()->getId().toActorId();
    this->_actorIndex = actorIndex;
    InitializeAllPaths();
}

Chat::Chat(QByteArray chatId, QByteArray key, BigNumber currentSession, ActorIndex* actorIndex,
           AccountController* accountController, QList<QByteArray> users, QByteArray ownerId)
{

    this->_chatId = chatId;
    this->_encryptionKey = key;
    this->_accountController = accountController;
    this->_currentActorId = accountController->getMainActor()->getId().toActorId();
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
void Chat::saveChatKey(QByteArray key, BigNumber sessionNumb)
{
    QFile file(pathToSession(sessionNumb) + "/key");
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(key);
        file.close();
        //        emit sendDataToBlockchain(getPathMyChatsKeyStore() + "key" + sessionNumb);
        return;
    }
    qDebug() << "[Error] Chat manager can't open file to save the key";
}
QByteArray Chat::unloadChatKey()
{
    QFile file(pathToSession(_currentSession) + "/key");
    if (!file.exists())
        return "0";
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray key = file.readLine();
        file.close();
        return key;
    }
    qDebug() << "[Error] Chat manager can't open file to load the key";
    return "0";
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
    QByteArray pathToUsers = getPathToUsers();
    QStringList usersList = QDir(getPathToUsers()).entryList(QDir::Files);
    QList<QByteArray> res;
    for (QString user : usersList)
        res.append(user.toUtf8());
    return res;
}

QList<UIMessage> Chat::getAllMessages()
{

    QList<QByteArray> allMessages = getAllMessagesByteArray();
    if (allMessages.empty())
        return {};
    QList<UIMessage> result;
    QList<QByteArray> currentData;

    QByteArray decryptedCurrentMessage;
    for (QByteArray msginList : allMessages)
    {
        decryptedCurrentMessage = decryptMessage(msginList);
        currentData = Serialization::universalDeserialize(decryptedCurrentMessage);
        if (currentData.size() == 2)
            result.append(UIMessage{ currentData.at(0), currentData.at(1) });

        else

            qDebug() << "[Error] Size !=2 in getAllMessages Chat";
    }
    return result;
}

QList<QByteArray> Chat::getAllMessagesByteArray()
{
    QList<QByteArray> list;
    QFile file(pathToSession(_currentSession) + "/session");
    if (file.open(QIODevice::ReadOnly))
    {

        list = Serialization::universalDeserialize(file.readAll());
        file.close();
        return list;
    }
    qDebug() << "[Error] File with session doesn't open. getAllMessagesByteArray Chat";
    return {};
}

ActorIndex* Chat::getActorIndex() const
{
    return _actorIndex;
}

QByteArray Chat::getOwner()
{
    return "-1";
}

QByteArray Chat::encryptByChatKey(QByteArray data)
{
    return encryptMessage(data);
}

QByteArray Chat::decryptByChatKey(QByteArray data)
{
    return encryptByChatKey(data);
}

QByteArray Chat::getPathCurrentChat()
{
    return ChatStorage::STORED_CHATS + _chatId + "/";
}

// QByteArray Chat::getPathMyChatsKeyStore()
//{
//    return getPathMyChatsCurrentChat() + "keystore/";
//}

QByteArray Chat::getPathToUsers()
{
    return ChatStorage::STORED_CHATS + _chatId + "/" + _currentSession.toByteArray() + "/users/";
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
    QDir().mkpath(getPathToUsers());
    QDir().mkpath(pathToSession(_currentSession));
    QDir().mkpath(getPathToUsers());
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
    saveChatKey(key, this->_currentSession);
    if (!isUserExist(_currentActorId, users))
        users.append(_currentActorId);
    loadUsers(users);
    QFile data(pathToSession(this->_currentSession) + "/session");
    data.open(QIODevice::WriteOnly);
    data.flush();
    data.close();
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

    for (QByteArray user : userList)
    {
        QFile userfile(getPathToUsers() + user);
        userfile.open(QIODevice::WriteOnly);
        userfile.flush();
        userfile.close();
    }
}

bool Chat::isUserExist(QByteArray actorId, QList<QByteArray> userList)
{
    for (QByteArray user : userList)

        if (user == actorId)
            return true;

    return false;
}

void Chat::sendMessage(QByteArray message)
{ //    // test
    //    QByteArray needToencrypt = "fwefwefwefwefwefwef";
    //    QByteArray encrypt = blowFish_crypt().EncryptBlowFish(needToencrypt, "12345453");
    //    QByteArray decrypt = blowFish_crypt().DecryptBlowFish(encrypt, "12345453");

    //    QByteArray messageeee;
    //    QList<QByteArray> list2;
    //    if (!allmessageList.empty())
    //        for (QByteArray msginList : allmessageList)
    //        {
    //            messageeee = decryptMessage(msginList);
    //            list2 = Serialization::universalDeserialize(messageeee);
    //            if (list2.size() != 2)
    //            {
    //                qDebug() << "[Error] Size !=2 bla bla send message";
    //            }
    //            else
    //            {
    //                qDebug() << "Message: " << list2.at(0) << " : " << list2.at(1);
    //            }
    //        }
    //    // test end

    QList<QByteArray> allmessageList = getAllMessagesByteArray();

    QList<QByteArray> currentMessage;
    currentMessage.append(_currentActorId);
    currentMessage.append(message);
    allmessageList.append(encryptMessage(Serialization::universalSerialize(currentMessage)));

    QFile file(pathToSession(_currentSession) + "/session");
    if (file.open(QIODevice::WriteOnly))
    {
        //        qDebug() << "KeyPRivate ewfwe=" << getChatPrivateKey();
        //        qDebug() << "message=" << message;
        //        qDebug() << "EncryptMEssage=" << encryptMessage(message);
        //        qDebug() << "Decrypt message=" << decryptMessage(encryptMessage(message));

        file.write(Serialization::universalSerialize(allmessageList));

        file.close();
        //   emit sendDataToBlockchain(getPathToSessions() + getMyCurrentSession());
    }
    else

        qDebug() << "[Warning] Cannot open file with session to send message. SendMessage, Chat";
}

void Chat::receiveMessage(QByteArray message)
{
    QFile file(pathToSession(_currentSession) + "/session");
    if (file.open(QIODevice::Append))
    {

        message = encryptMessage(message) + "\n";
        file.write(message);

        file.close();
        //   emit sendDataToBlockchain(getPathToSessions() + getMyCurrentSession());
    }
    else

        qDebug() << "[Warning] Cannot open file with session to send message. SendMessage, Chat";
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
        users.append((actorId));
        loadUsers(users);
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
    return ChatStorage::STORED_CHATS + _chatId + "/" + sessionNumber.toByteArray() + "/";
}

Chat::~Chat()
{
}
