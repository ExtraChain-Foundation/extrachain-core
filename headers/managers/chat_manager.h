#ifndef CHAT_MANAGER_H
#define CHAT_MANAGER_H
#define chatStore "pathToChatStore/"
#define myChatKeys "pathToMyChatKey/" // should be outside the blockchain
#include "QByteArray"
#include <QFile>
#include <QDebug>
//#include "utils/bignumber.h"
#include "utils/utils.h"
class ChatManager
{
private:
    QByteArray _chatId = "0";
    QByteArray _encryptionKey = "0";

private:
    QByteArray generateChatKey();
    bool isValid(BigNumber chatId); // if chat id is not occupied
    void saveMyChatPrivateKey(QByteArray key);
    QByteArray unloadMyChatPrivateKey();

public:
    ChatManager(QByteArray chatId);
    bool createNewChat();
    void saveChatKey(QByteArray key);
    QByteArray unloadChatKey();
    void addMemberToChat(BigNumber actorId);
    void removeMemberFromChat(BigNumber actorId);
    void sendMessage(QByteArray message);
    QByteArray receiveMessage(QByteArray message);
    QByteArray encryptMessage(QByteArray message);
    QByteArray decryptMessage(QByteArray message);
    ~ChatManager();
};

#endif // CHAT_MANAGER_H
