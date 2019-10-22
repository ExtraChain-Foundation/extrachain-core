#ifndef CHAT_MANAGER_H
#define CHAT_MANAGER_H
#define chatStore "pathToChatStore/"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "enc/algorithms/blowfish_crypt.h"
#include <QDir>
#include <QObject>
class ChatManager : public QObject
{
    Q_OBJECT
private:
    QByteArray _chatId = "0";
    QByteArray _encryptionKey = "0";
    QByteArray _currentSession = "-1";
    AccountController* _accountController;

private:
    QByteArray generateChatKey();                             //+
    QByteArray generateChatId();                              //+
    bool isValid(BigNumber chatId);                           //+
    QByteArray getCurrentSession();                           //+
    QByteArray findCurrentSession();                          //+
    QByteArray createNewSession(QByteArray key);              //+
    QByteArray getChatPrivateKey();                           //+
    void saveChatKey(QByteArray key, QByteArray sessionNumb); //+
    QByteArray unloadChatKey();                               //+
    QByteArray encryptMessage(QByteArray message);            //+
    QByteArray decryptMessage(QByteArray message);            //+

public:
    ChatManager(QByteArray chatId, AccountController* accountController); //+
    void createNewChat();                                                 //+

    void addMemberToChat(BigNumber actorId);       //+
    void removeMemberFromChat(BigNumber actorId);  //-
    void sendMessage(QByteArray message);          //-
    QByteArray receiveMessage(QByteArray message); //+

    ~ChatManager()
    {
        delete _accountController;
    }

public slots:
    void receiveInviteToChat(QByteArray chatId, QByteArray sessionNumb, QByteArray key); //+
signals:
    void sendInviteToChat(QByteArray chatId, QByteArray sessionNumb, BigNumber actorId, QByteArray key); //+
    void sendCreatedNewChat(QByteArray chatId);
    void sendMessageToChat(QByteArray chatId, QByteArray sessionNumb, BigNumber senderId, QByteArray message);
};

#endif // CHAT_MANAGER_H
// isValid fix. Fix createNewChat+ Incapsulatuon + Signal SendMessage
