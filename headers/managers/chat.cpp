#include "chat.h"

Chat::Chat(QByteArray chatId, AccountController* accountController, QByteArray chatPath)
{
     InitializeOwnerPathNewChat();
    this->_chatId = chatId;
    this->_encryptionKey = unloadChatKey();
    this->_accountController = accountController;
    this->_actorPath = accountController->getActorIndex()->getFolderPath().toLocal8Bit();
    this->_currentSession = getMyCurrentSession();
    this->_currentActorId = accountController->getMainActor()->getId().toActorId();
    this->_chatPath = chatPath;

}

Chat::Chat(QByteArray chatId, QByteArray key, QByteArray currentSession, AccountController* accountController,
           QByteArray chatPath, QByteArray ownerId)
{
    this->_chatId = chatId;
    this->_encryptionKey = key;
    this->_accountController = accountController;
    this->_actorPath = accountController->getActorIndex()->getFolderPath().toLocal8Bit();
    this->_currentActorId = accountController->getMainActor()->getId().toActorId();
    this->_currentSession = currentSession;
    this->_chatPath = chatPath;
    InitializeOwnerPathNewChat();
    QFile file(getPathToSessions() + "0");
    file.open(QIODevice::WriteOnly);
    file.close();
    emit sendDataToBlockchain(getPathToSessions() + "0"); // creation 0 session
    if (ownerId != "-1")
    {
        file.setFileName(getPathToUsers() + ownerId);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write("owner");
            file.close();
            emit sendDataToBlockchain(getPathToUsers() + ownerId);
        }
        else
            qDebug() << "[Error] Cannot create Chat owner";
    }
    createNewSession(key, currentSession);
}

Chat::Chat(const Chat& tempChat)
{
        InitializeOwnerPathNewChat();
    this->_chatId = tempChat.getChatId();
    this->_encryptionKey = tempChat.getEncryptionKey();
    this->_currentSession = tempChat.getSession();
    this->_accountController = tempChat.getAccountController();
    this->_actorPath = tempChat.getActorPath();
    this->_currentActorId = tempChat.getCurrentActorId();
    this->_chatPath = tempChat.getChatPath();

}

bool Chat::isOwner()
{
    return QFile(_actorPath + _currentActorId + "/chatStorage/" + _chatId + "/users/" + _currentActorId)
        .exists();
}

bool Chat::isUserActual(QByteArray actorId, QByteArray sessionNumb)
{
    QFile file(_actorPath + actorId + "/myChats/" + _chatId + "/currentSession");
    if (!file.exists())
        return false;
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray actorSession = file.readLine();
        file.close();
        return sessionNumb == actorSession;
    }
    else
    {
        qDebug() << "[Warning] Cann't open file on read. isUserActual in Chat. path="
                 << _actorPath + actorId + "/myChats/" + _chatId + "/currentSession";
        return false;
    }
}
void Chat::saveChatKey(QByteArray key, QByteArray sessionNumb)
{
    QFile file(getPathMyChatsKeyStore() + "key" + sessionNumb);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(key);
        file.close();
        emit sendDataToBlockchain(getPathMyChatsKeyStore() + "key" + sessionNumb);
        return;
    }
    qDebug() << "[Error] Chat manager can't open file to save the key";
}
QByteArray Chat::unloadChatKey()
{
    QFile file(getPathMyChatsKeyStore() + "key" + getMyCurrentSession());
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

QByteArray Chat::getMyCurrentSession()
{
    if (this->_currentSession != "-1")
        return this->_currentSession;
    QFile file(getPathMyChatsCurrentChat() + "currentSession");
    if (!file.exists())
    {
        QByteArray currentSession = "0";
        if (file.open(QIODevice::WriteOnly))
        {
            currentSession = findCurrentSession();
            file.write(currentSession);
            file.close();
            this->_currentSession = currentSession;
            emit sendDataToBlockchain(getPathMyChatsCurrentChat() + "currentSession");
            return currentSession;
        }
        else
            qDebug() << "[Warning] cannot open file to write session in chat manager";
        return "-1";
    }

    if (file.open(QIODevice::ReadOnly))
    {
        this->_currentSession = file.readLine();
        file.close();
        return this->_currentSession;
    }
    qDebug() << "[Error] Chat manager can't open file to load the key";
    return "-1";
}
QByteArray Chat::getActorPath() const
{
    return _actorPath;
}

QByteArray Chat::getCurrentActorId() const
{
    return _currentActorId;
}

QByteArray Chat::getChatPath() const
{
    return _chatPath;
}

QStringList Chat::getAllUsers()
{
    QStringList usersList;
      QDirIterator it(   getPathToUsers(), QDirIterator::Subdirectories);
      while (it.hasNext())
      {
          usersList.append(it.fileName().toLocal8Bit());

          it.next();
      }
      return usersList;
}

QByteArray Chat::getPathMyChatsCurrentChat()
{
    return _actorPath + _currentActorId + "/myChats/" + _chatId + "/";
}

QByteArray Chat::getPathMyChatsKeyStore()
{
    return getPathMyChatsCurrentChat() + "keystore/";
}

QByteArray Chat::getPathToUsers()
{
    return _chatPath + "/users/";
}

QByteArray Chat::getPathToSessions()
{
    return _chatPath + "/sessions/";
}

QByteArray Chat::findCurrentSession()
{
    BigNumber currentSession("-1");
    QFile file;
    do
    {
        currentSession++;
        file.setFileName(getPathToSessions() + currentSession.toByteArray());
    } while (file.exists());
    currentSession--;
    return currentSession.toByteArray();
}

void Chat::InitializeOwnerPathNewChat()
{
    QDir().mkpath(getPathToUsers());
    QDir().mkpath(getPathToSessions());
    QDir().mkpath(getPathMyChatsKeyStore());
}
QByteArray Chat::createNewSession(QByteArray key, QByteArray sessionNumb)
{
    if (!SaveSession((getPathMyChatsCurrentChat() + "currentSession"), sessionNumb))
        return "-1";
    saveChatKey(key, sessionNumb);
    return sessionNumb;
}
QByteArray Chat::getChatPrivateKey()
{
    return _accountController->getMainActor()->getKey()->decrypt(unloadChatKey());
}

QByteArray Chat::getActualCurrentSession()
{
    return findCurrentSession();
}
QByteArray Chat::encryptMessage(QByteArray message)
{
    return blowFish_crypt().EncryptBlowFish(message, getChatPrivateKey());
}
QByteArray Chat::decryptMessage(QByteArray message)
{
    return encryptMessage(message);
}

bool Chat::SaveSession(QByteArray sessionPath, QByteArray sessionNumb)
{
    QFile file(sessionPath);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(sessionNumb);
        file.close();
        emit sendDataToBlockchain(sessionPath);
        return true;
    }

    qDebug() << "[Warning] Error when write sessionpath" << sessionPath << " sessionnumb " << sessionNumb;
    return false;
}

void Chat::sendMessage(QByteArray message)
{
    message.push_front(_currentActorId + ": ");
    QFile file(getPathToSessions() + getMyCurrentSession());
    if (file.open(QIODevice::Append))
    {
        message=encryptMessage(message);
        message.push_back("/n");
        file.write(encryptMessage(message));
        file.close();
        emit sendDataToBlockchain(getPathToSessions() + getMyCurrentSession());
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

QByteArray Chat::getSession() const
{
    return this->_currentSession;
}

AccountController* Chat::getAccountController() const
{
    return this->_accountController;
}

void Chat::InviteNewUser(QByteArray inviterSign, QByteArray actorId)
{
    QDir().mkpath(_actorPath + actorId + "/myChats/" + _chatId + "/");
    QFile file(_actorPath + actorId + "/myChats/" + _chatId + "/" + _chatId + ".dat");
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(_chatPath);
        file.close();
        emit sendDataToBlockchain(_actorPath + actorId + "/myChats/" + _chatId + "/" + _chatId + ".dat");
    }
    else
    {
        qDebug() << "[Warning] Error open file on write when invite new user "
                 << _actorPath + actorId + "/myChats/" + _chatId + "/" + _chatId + ".dat";
    }
    file.setFileName(_actorPath + actorId + "/myChats/" + _chatId + "/currentSession");
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(getMyCurrentSession());
        file.close();
        emit sendDataToBlockchain(_actorPath + actorId + "/myChats/" + _chatId + "/currentSession");
    }
    else
    {
        qDebug() << "[Warning] Error open file on write when invite new user "
                 << _actorPath + actorId + "/myChats/" + _chatId + "/currentSession";
    }
    QDir().mkpath(_actorPath + actorId + "/myChats/" + _chatId + "/keystore/");
    file.setFileName(_actorPath + actorId + "/myChats/" + _chatId + "/keystore/key" + getMyCurrentSession());
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(inviterSign);
        file.close();
        emit sendDataToBlockchain(_actorPath + actorId + "/myChats/" + _chatId + "/keystore/key"
                                  + getMyCurrentSession());
    }
    else
    {
        qDebug() << "[Warning] Error open file on write when invite new user "
                 << _actorPath + actorId + "/myChats/" + _chatId + "/keystore/key" + getMyCurrentSession();
    }

    QList<QByteArray> signData;
    signData.append(_currentActorId);
    signData.append(inviterSign);
    file.setFileName(getPathToUsers() + actorId);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(Serialization::universalSerialize(signData));
        file.close();
        emit sendDataToBlockchain(getPathToUsers() + actorId);
        return;
    }
    else
        qDebug() << "[Error] when try to write data about new user";
}
bool Chat::isUserVerify(QByteArray actorId) // CYCLE instead of recursive?!?!?!?!?!?!?!?!
{
    QFile file(getPathToUsers() + actorId);
    if (!file.exists())
        return false;
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray data = file.readLine();
        if (data == "owner" || data == "owner\n")
            return true;
        QList<QByteArray> list = Serialization::universalDeserialize(data);
        if (list.size() != 2)
            return false;
        if (!_accountController->getActor(BigNumber(list.at(0))).getKey()->verify(list.at(0), list.at(1)))
            return false;
        return isUserVerify(list.at(0));
    }
    qDebug() << "[Error] Cannot open file for user verify in chat manager.";
    return false;
}

Chat::~Chat()
{

}
