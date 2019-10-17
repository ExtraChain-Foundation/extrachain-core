#include "chat_manager.h"

ChatManager::ChatManager(QByteArray chatId)
{
    this->_chatId = chatId;
    this->_encryptionKey = loadChatKey();
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

QByteArray ChatManager::loadChatKey()
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
}

void ChatManager::removeMemberFromChat(BigNumber actorId)
{
}

QByteArray ChatManager::encryptMessage(QByteArray message)
{
    return "encryptMessage";
}

QByteArray ChatManager::decryptMessage(QByteArray message)
{
    return encryptMessage(message);
}
