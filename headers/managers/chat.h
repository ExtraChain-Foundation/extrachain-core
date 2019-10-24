#ifndef CHAT_H
#define CHAT_H
#define chatStore "pathToChatStore/"
#define keyStore "pathToKeyStore/" // local store
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "enc/algorithms/blowfish_crypt.h"
#include <QDir>
#include <QObject>
class Chat : public QObject
{
    Q_OBJECT
private:
    QByteArray _chatId = "0";
    QByteArray _encryptionKey = "0";
    QByteArray _currentSession = "-1";
    AccountController* _accountController;

private:
    QByteArray findCurrentSession();                                  //+
    void saveChatKey(QByteArray key, QByteArray sessionNumb);         //+
    QByteArray encryptMessage(QByteArray message);                    //+
    QByteArray decryptMessage(QByteArray message);                    //+
    bool SaveSession(QByteArray sessionPath, QByteArray sessionNumb); //+
public:
    Chat(QByteArray chatId, AccountController* accountController); //+
    Chat(QByteArray chatId, QByteArray key, QByteArray currentSession, AccountController* accountController,
         QByteArray ownerId = "-1"); //+
    Chat(const Chat& tempChat);      //+

    QByteArray unloadChatKey();                                          //+
    QByteArray getChatPrivateKey();                                      //+
    QByteArray getCurrentSession();                                      //+
    QByteArray createNewSession(QByteArray key, QByteArray sessionNumb); //+
    void sendMessage(QByteArray message);                                //-
    QByteArray receiveMessage(QByteArray message);                       //-
    // getters setters
    QByteArray getChatId() const;                    //+
    QByteArray getEncryptionKey() const;             //+
    QByteArray getSession() const;                   //+
    AccountController* getAccountController() const; //+
    void InviteNewUser(QByteArray inviterId, QByteArray inviterSign, QByteArray invitedId);
    bool isUserVerify(QByteArray actorId);
    void removeUserFromChat(QByteArray actorId);
    ~Chat(); //+
signals:

    void sendMessageToChat(QByteArray chatId, QByteArray sessionNumb, BigNumber senderId,
                           QByteArray message); //+
};

#endif // CHAT_MANAGER_H
