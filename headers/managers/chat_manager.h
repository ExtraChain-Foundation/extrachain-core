#ifndef CHAT_MANAGER_H
#define CHAT_MANAGER_H
#define chatStore "pathToChatStore/"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "enc/algorithms/blowfish_crypt.h"
#include <QDir>
class ChatManager
{
private:
    QByteArray _chatId = "0";
    QByteArray _encryptionKey = "0";
    AccountController* _accountController;

private:
    QByteArray generateChatKey();                //+
    QByteArray generateChatId();                 //+
    bool isValid(BigNumber chatId);              //-
    QByteArray getCurrentSession();              //+
    QByteArray findCurrentSession();             //+
    QByteArray createNewSession(QByteArray key); //+
    QByteArray getChatPrivateKey();              //+

public:
    ChatManager(QByteArray chatId, AccountController* accountController); //+
    void createNewChat();                                                 //-
    void saveChatKey(QByteArray key);                                     //+
    QByteArray unloadChatKey();                                           //+
    void addMemberToChat(BigNumber actorId);                              //-
    void removeMemberFromChat(BigNumber actorId);                         //-
    void sendMessage(QByteArray message);                                 //-
    QByteArray receiveMessage(QByteArray message);                        //+
    QByteArray encryptMessage(QByteArray message);                        //+
    QByteArray decryptMessage(QByteArray message);                        //+
    ~ChatManager();
};

#endif // CHAT_MANAGER_H
