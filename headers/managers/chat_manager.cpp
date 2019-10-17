#include "chat_manager.h"

ChatManager::ChatManager(QByteArray chatId)
{
    this->_chatId = chatId;
    this->_encryptionKey = unloadChatKey();
}

bool ChatManager::createNewChat()
{
    if (isValid(_chatId))
    {
        QByteArray privateChatKey = generateChatKey();
        saveMyChatPrivateKey(privateChatKey);
        // send message in blockhain about chat creation
        return true;
    }
    qDebug() << "[Error] createNewChat. Chat id is not valid";
    return false;
}

void ChatManager::saveChatKey(QByteArray key)
{
    QFile file(chatStore + this->_chatId);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(key);
        file.close();
        return;
    }
    qDebug() << "[Error] Chat manager can't open file to save the key";
}

void ChatManager::saveMyChatPrivateKey(QByteArray key)
{
    QFile file(myChatKeys + this->_chatId);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(key);
        file.close();
        return;
    }
    qDebug() << "[Error] Chat manager can't open file to save my key";
}

QByteArray ChatManager::unloadMyChatPrivateKey()
{
    QFile file(myChatKeys + this->_chatId);
    if (!file.exists())
        return "0";
    if (file.open(QIODevice::ReadOnly))
        return file.readLine();
    qDebug() << "[Error] Chat manager can't open file to load my key";
    return "0";
}

QByteArray ChatManager::unloadChatKey()
{
    QFile file(chatStore + this->_chatId);
    if (!file.exists())
        return "0";
    if (file.open(QIODevice::ReadOnly))
        return file.readLine();
    qDebug() << "[Error] Chat manager can't open file to load the key";
    return "0";
}

void ChatManager::addMemberToChat(BigNumber actorId)
{
    if (unloadMyChatPrivateKey() != "0")
    {
        // QByteArray
        // memberKey=ActorIndex->GetActorById(actorId)->getPublicKey().encrypt(unloadMyChatPrivateKey());
        // send in blockhaind member key
    }
}

void ChatManager::removeMemberFromChat(BigNumber actorId)
{
    // regenerate key and create new session
}

void ChatManager::sendMessage(QByteArray message)
{
    // XOR message with _encryptionKey and send it to blockchain
}

QByteArray ChatManager::receiveMessage(QByteArray message)
{
    // XOR message with _encryptionKey and get it
    return message;
}

QByteArray ChatManager::generateChatKey()
{
    return Utils::calcKeccak(BigNumber::random(64).toByteArray());
}

bool ChatManager::isValid(BigNumber chatId)
{
    // if chat id is accessible for create chat with that id, then return true
    return true;
}

QByteArray ChatManager::encryptMessage(QByteArray message)
{
    return "encryptMessage";
}

QByteArray ChatManager::decryptMessage(QByteArray message)
{
    return encryptMessage(message);
}
