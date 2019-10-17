#ifndef CHAT_MANAGER_H
#define CHAT_MANAGER_H
#define chatStore "pathToChatStore/"
#include "QByteArray"
#include <QFile>
#include <QDebug>
#include "utils/bignumber.h"
class ChatManager
{
private:
    QByteArray _chatId = "0";
    QByteArray _encryptionKey = "0";

public:
    ChatManager(QByteArray chatId);
    void saveChatKey(QByteArray key);
    QByteArray loadChatKey();
    void addMemberToChat(BigNumber actorId);
    void removeMemberFromChat(BigNumber actorId);
    QByteArray encryptMessage(QByteArray message);
    QByteArray decryptMessage(QByteArray message);
    ~ChatManager();
};

#endif // CHAT_MANAGER_H
