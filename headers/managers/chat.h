#ifndef CHAT_H
#define CHAT_H
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "enc/algorithms/blowfish_crypt.h"
#include <QDir>
#include <QDirIterator>
#include <QObject>

struct UIChat
{
    QStringList users;
    QString chatId;
};

struct UIMessage
{
    QString userId;
    QString message;

};

class Chat : public QObject
{
    Q_OBJECT
private:
    QByteArray _chatId = "0";
    QByteArray _encryptionKey = "0";
    QByteArray _currentSession = "-1";
    QByteArray _chatPath = "-1";      // blockhain/index/actors/[ownerId]/chatStorage/[chatId]/
    QByteArray _actorPath = "blabla"; // blockhain/index/actors/
    QByteArray _currentActorId = "-1";
    AccountController* _accountController;
    ActorIndex * _actorIndex;

private:
    // paths getters:
    QByteArray getPathMyChatsCurrentChat(); //+ blockhain/index/actors/[myId]/myChats/[chatId]/
    QByteArray getPathMyChatsKeyStore();    //+ blockhain/index/actors/[myId]/myChats/[chatId]/keystore/
    QByteArray getPathToUsers();            //+ blockhain/index/actor/[ownerId]/chatStorage/[chatId]/users/
    QByteArray getPathToSessions(); //+ blockhain/index/actors/[ownerId]/chatStorage/[chatId]/sessions/
    // paths end
    QByteArray findCurrentSession(); //+
    void InitializeOwnerPathNewChat();
    void saveChatKey(QByteArray key, QByteArray sessionNumb);         //+
    QByteArray encryptMessage(QByteArray message);                    //+
    QByteArray decryptMessage(QByteArray message);                    //+
    bool SaveSession(QByteArray sessionPath, QByteArray sessionNumb); //+

public:
    Chat(QByteArray chatId,ActorIndex *actorIndex, AccountController* accountController, QByteArray chatPath);
    Chat(QByteArray chatId, QByteArray key, QByteArray currentSession,ActorIndex *actorIndex, AccountController* accountController,
         QByteArray chatPath, QByteArray ownerId = "-1"); //+
    Chat(const Chat& tempChat);                           //+
    bool isOwner();                                       //+
    bool isUserActual(QByteArray actorId, QByteArray sessionNumb);
    QByteArray unloadChatKey();                                          //+
    QByteArray getChatPrivateKey();                                      //+
    QByteArray getActualCurrentSession();                                //+
    QByteArray getMyCurrentSession();                                    //+
    QByteArray createNewSession(QByteArray key, QByteArray sessionNumb); //+
    void sendMessage(QByteArray message);                                //+
    // getters setters
    QByteArray getChatId() const;                                   //+
    QByteArray getEncryptionKey() const;                            //+
    QByteArray getSession() const;                                  //+
    AccountController* getAccountController() const;                //+
    void InviteNewUser(QByteArray inviterSign, QByteArray actorId); //+
    bool isUserVerify(QByteArray actorId);                          //+????
    ~Chat();                                                        //+
    QByteArray getActorPath() const;                                //+
    QByteArray getCurrentActorId() const;                           //+
    QByteArray getChatPath() const;                                 //+
    QStringList getAllUsers();
    QByteArray getAllMessages();
    ActorIndex *getActorIndex() const;

signals:
    void sendDataToBlockchain(const QString& path); //+ send to blockchain. Connect with ChatManager
};

#endif // CHAT_MANAGER_H
