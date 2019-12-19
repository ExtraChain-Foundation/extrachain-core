#ifndef CHAT_H
#define CHAT_H
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "enc/algorithms/blowfish_crypt.h"
#include "utils/db_connector.h"
#include <QDir>
#include <QDirIterator>
#include <QObject>

struct UIMessage
{
    QString userId;
    QString message;
    QDateTime date;
};

struct UIChat
{
    QStringList users;
    QString chatId;
    UIMessage lastMessage;
};

class Chat : public QObject
{
    Q_OBJECT
private:
    QByteArray ownerID = "-1";
    QByteArray _chatId = "0";
    QByteArray _encryptionKey = "0";
    BigNumber _currentSession = -1;
    QByteArray _currentActorId = "-1";
    AccountController* _accountController;
    ActorIndex* _actorIndex;

private:
    // paths getters:
    QByteArray getPathCurrentChat();                   //+ keystore/chats/[chatId]/
    QByteArray getPathToUsers();                       //+  keystore/chats/[chatId]/[sessionId]/users/
    QByteArray pathToSession(BigNumber sessionNumber); //+  keystore/chats/[chatId]/[sessionId]
    // paths end
    BigNumber findCurrentSession();                                              //+
    void InitializeAllPaths();                                                   //+
    void saveChatKey(QByteArray key, BigNumber sessionNumb);                     //+
                                                                                 //+
    void loadUsers(QList<QByteArray> userList, QList<QByteArray> userData = {}); //+
    bool isUserExist(QByteArray actorId, QList<QByteArray> userList);            //+

public:
    Chat(QByteArray chatId, ActorIndex* actorIndex, AccountController* accountController,
         BigNumber sessionNumb = -1); //+
    Chat(QByteArray chatId, QByteArray key, BigNumber currentSession, ActorIndex* actorIndex,
         AccountController* accountController, QList<QByteArray> users, QByteArray ownerId = "-1"); //+
    Chat(const Chat& tempChat);                                                                     //+
    ~Chat();
    bool isOwner();                                               //-
    bool isUserActual(QByteArray actorId, BigNumber sessionNumb); //-
    QByteArray unloadChatKey();                                   //+
    // QByteArray getChatPrivateKey();                               //+
    BigNumber getActualCurrentSession(); //+
    // BigNumber getMyCurrentSession();                              //
    bool createNewSession(QByteArray key, QList<QByteArray> users = {},
                          QByteArray ownerId = "-1"); //+
    QByteArray sendMessage(QByteArray message);       //+
    void receiveMessage(QByteArray message);
    // getters setters
    QByteArray getChatId() const;                    //+
    QByteArray getEncryptionKey() const;             //+
    BigNumber getSession() const;                    //+
    AccountController* getAccountController() const; //+
    void InviteNewUser(QByteArray actorId);          //+-
    bool isUserVerify(QByteArray actorId);           //?-

    QByteArray getCurrentActorId() const; //+
    QList<QByteArray> getAllUsers();      //+
    QList<UIMessage> getAllMessages();    //-
    QList<QByteArray> getAllMessagesByteArray();
    ActorIndex* getActorIndex() const; //+
    QByteArray getOwner();             //-
    QByteArray encryptByChatKey(QByteArray data);
    QByteArray decryptByChatKey(QByteArray data);
    UIMessage getLastMessage();
    void removeAllChatData();
    QByteArray encryptMessage(QByteArray message); //+
    QByteArray decryptMessage(QByteArray message);

signals:
    void sendDataToBlockchain(const QString& path); // send to blockchain. Connect with ChatManager
};

#endif // CHAT_MANAGER_H
