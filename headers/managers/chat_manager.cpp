#include "chat_manager.h"
void ChatManager::createNewChat()
{
    do
    {
        _chatId = generateChatId();
    } while (!isValid(_chatId));
    QDir().mkpath(chatStore + _chatId + "/");
    QFile file(chatStore + _chatId + "/0");
    file.open(QIODevice::WriteOnly);
    file.close();
    QByteArray privateChatKey = generateChatKey();
    saveChatKey(
        KeyPublic(_accountController->getCurrentActor().getKey()->getPublicKey()).encrypt(privateChatKey),
        getCurrentSession());
    emit sendCreatedNewChat(_chatId);
}
void ChatManager::saveChatKey(QByteArray key, QByteArray sessionNumb)
{
    QFile file(chatStore + this->_chatId + "/key" + sessionNumb);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(key);
        file.close();
        return;
    }
    qDebug() << "[Error] Chat manager can't open file to save the key";
}
ChatManager::ChatManager(QByteArray chatId, AccountController* accountController)
{
    this->_chatId = chatId;
    this->_encryptionKey = unloadChatKey();
    this->_accountController = accountController;
}
QByteArray ChatManager::unloadChatKey()
{
    QFile file(chatStore + this->_chatId + "/key" + getCurrentSession());
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
void ChatManager::addMemberToChat(BigNumber actorId)
{
    QByteArray key = unloadChatKey();
    if (key != "0")
    {
        key = KeyPublic(_accountController->getActor(actorId).getKey()->getPublicKey())
                  .encrypt(getChatPrivateKey());
        emit sendInviteToChat(this->_chatId, getCurrentSession(), actorId, key);
    }
}
void ChatManager::removeMemberFromChat(BigNumber actorId)
{
    QByteArray key = generateChatKey();
    if (createNewSession(
            KeyPublic(_accountController->getCurrentActor().getKey()->getPublicKey()).encrypt(key))
        != "-1")
    {
        // send key to all member without actorId in blockchain
        //    for(int i=0;i<quantityChatMember;i++)
        //    {
        // if(i!=actorId)
        //        sendInviteToChat(chatid,session,i,key)
        //    }
    }
    else
        qDebug() << "Error when remove Member from chat";
}
QByteArray ChatManager::generateChatKey()
{
    return Utils::calcKeccak(BigNumber::random(64).toByteArray());
}
QByteArray ChatManager::generateChatId()
{
    return generateChatKey();
}
bool ChatManager::isValid(BigNumber chatId)
{
    return !QDir(chatStore + chatId.toByteArray()).exists();
}
QByteArray ChatManager::getCurrentSession()
{
    if (this->_currentSession != "-1")
        return this->_currentSession;
    QFile file(chatStore + this->_chatId + "/" + "currentSession");
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
QByteArray ChatManager::findCurrentSession()
{
    BigNumber currentSession("-1");
    QFile file;
    do
    {
        currentSession++;
        file.setFileName(chatStore + this->_chatId + "/" + currentSession.toByteArray());
    } while (file.exists());
    currentSession--;
    return currentSession.toByteArray();
}
QByteArray ChatManager::createNewSession(QByteArray key)
{
    QByteArray newSession = (BigNumber(getCurrentSession()) + BigNumber("1")).toByteArray();
    QFile file(chatStore + this->_chatId + "/" + "currentSession");
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(newSession);
        file.close();
        saveChatKey(key, newSession);
        return newSession;
    }
    qDebug() << "[Warning] Cannot open session file when try to create new Session";
    return "-1";
}
QByteArray ChatManager::getChatPrivateKey()
{
    return _accountController->getCurrentActor().getKey()->decrypt(unloadChatKey());
}
QByteArray ChatManager::encryptMessage(QByteArray message)
{
    return blowFish_crypt().EncryptBlowFish(message, getChatPrivateKey());
}
QByteArray ChatManager::decryptMessage(QByteArray message)
{
    return encryptMessage(message);
}
void ChatManager::receiveInviteToChat(QByteArray chatId, QByteArray sessionNumb, QByteArray key)
{
    this->_chatId = chatId;
    QDir().mkpath(chatStore + _chatId + "/");
    this->_encryptionKey = key;
    this->_currentSession = sessionNumb;
    saveChatKey(key, sessionNumb);
}
void ChatManager::sendMessage(QByteArray message)
{
    emit sendMessageToChat(this->_chatId, getCurrentSession(), _accountController->getCurrentActor().getId(),
                           encryptMessage(message));
}

QByteArray ChatManager::receiveMessage(QByteArray message)
{
    return decryptMessage(message);
}
