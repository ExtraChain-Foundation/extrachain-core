#include "chat.h"

Chat::Chat(QByteArray chatId, AccountController* accountController, QByteArray chatPath)
{
    this->_chatId = chatId;
    this->_encryptionKey = unloadChatKey();
    this->_accountController = accountController;
    this->_actorPath = accountController->getActorIndex()->getFolderPath().toLocal8Bit();
    this->_currentSession = getCurrentSession();
    this->_currentActorId = accountController->getCurrentActor().getId().toByteArray();
    this->_chatPath = chatPath;
}

Chat::Chat(QByteArray chatId, QByteArray key, QByteArray currentSession, AccountController* accountController,
           QByteArray chatPath, QByteArray ownerId)
{
    QDir().mkpath(getCurrentPathChatStore() + chatId + "/");
    QDir().mkpath(getCurrentPathChatStore() + chatId + "/users/");
    QDir().mkpath(keyStore + chatId + "/");
    QFile file(getCurrentPathChatStore() + chatId + "/0");
    file.open(QIODevice::WriteOnly);
    file.close();
    if (ownerId != "-1")
    {
        file.setFileName(getCurrentPathChatStore() + chatId + "/users/" + ownerId);
        if (file.open(QIODevice::WriteOnly))
            file.write("owner");
        else
            qDebug() << "[Error] Cannot create Chat owner";
        file.close();
    }
    this->_chatId = chatId;
    this->_encryptionKey = key;
    this->_accountController = accountController;
    this->_actorPath = accountController->getActorIndex()->getFolderPath().toLocal8Bit();
    this->_currentActorId = accountController->getCurrentActor().getId().toByteArray();
    this->_currentSession = currentSession;
    this->_chatPath = chatPath;
    createNewSession(key, currentSession);
}

Chat::Chat(const Chat& tempChat)
{
    this->_chatId = tempChat.getChatId();
    this->_encryptionKey = tempChat.getEncryptionKey();
    this->_currentSession = tempChat.getSession();
    this->_accountController = tempChat.getAccountController();
    this->_actorPath = tempChat.getActorPath();
    this->_currentActorId = tempChat.getCurrentActorId();
    this->_chatPath = tempChat.getChatPath();
}
void Chat::saveChatKey(QByteArray key, QByteArray sessionNumb)
{
    QFile file(keyStore + this->_chatId + "/key" + sessionNumb);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(key);
        file.close();
        return;
    }
    qDebug() << "[Error] Chat manager can't open file to save the key";
}
QByteArray Chat::unloadChatKey()
{
    QFile file(keyStore + this->_chatId + "/key" + getCurrentSession());
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

QByteArray Chat::getCurrentSession()
{
    if (this->_currentSession != "-1")
        return this->_currentSession;
    QFile file(getCurrentPathChatStore() + this->_chatId + "/" + "currentSession");
    if (!file.exists())
    {
        QByteArray currentSession = "0";
        if (file.open(QIODevice::WriteOnly))
        {
            currentSession = findCurrentSession();
            file.write(currentSession);
            file.close();
            this->_currentSession = currentSession;
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

QByteArray Chat::getCurrentPathChatStore()
{
    return this->_chatPath + "/" + chatStore;
}

QByteArray Chat::getCurrentPathKeyStore()
{
    return this->_actorPath + this->_currentActorId + "/" + chatStore;
}

QByteArray Chat::findCurrentSession()
{
    BigNumber currentSession("-1");
    QFile file;
    do
    {
        currentSession++;
        file.setFileName(getCurrentPathChatStore() + this->_chatId + "/" + currentSession.toByteArray());
    } while (file.exists());
    currentSession--;
    return currentSession.toByteArray();
}
QByteArray Chat::createNewSession(QByteArray key, QByteArray sessionNumb)
{
    if (!SaveSession((getCurrentPathChatStore() + this->_chatId + "/" + "currentSession"), sessionNumb))
        return "-1";
    if (!SaveSession((keyStore + this->_chatId + "/" + "currentSession"), sessionNumb))
        return "-1";
    saveChatKey(key, sessionNumb);
    return sessionNumb;
}
QByteArray Chat::getChatPrivateKey()
{
    return _accountController->getCurrentActor().getKey()->decrypt(unloadChatKey());
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
        return true;
    }

    qDebug() << "[Warning] Error when write sessionpath" << sessionPath << " sessionnumb " << sessionNumb;
    return false;
}

void Chat::sendMessage(QByteArray message)
{
    emit sendMessageToChat(this->_chatId, getCurrentSession(), _accountController->getCurrentActor().getId(),
                           encryptMessage(message));
}

QByteArray Chat::receiveMessage(QByteArray message)
{
    QByteArray receiveMessage = decryptMessage(message);
    // emit signall receive message
    return receiveMessage;
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

void Chat::InviteNewUser(QByteArray inviterId, QByteArray inviterSign, QByteArray invitedId)
{
    QList<QByteArray> signData;
    signData.append(inviterId);
    signData.append(inviterSign);
    QFile file(getCurrentPathChatStore() + this->_chatId + "/users/" + invitedId);
    if (file.open(QIODevice::WriteOnly))
        file.write(Serialization::universalSerialize(signData));
    else
        qDebug() << "[Error] when try to write data about new user";
    file.close();
}
bool Chat::isUserVerify(QByteArray actorId) // CYCLE instead of recursive?!?!?!?!?!?!?!?!
{
    QFile file(getCurrentPathChatStore() + this->_chatId + "/users/" + actorId);
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

void Chat::removeUserFromChat(QByteArray actorId)
{
    QFile file(getCurrentPathChatStore() + this->_chatId + "/users/" + actorId);
    file.remove();
}

Chat::~Chat()
{
    delete _accountController;
}
