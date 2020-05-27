#ifndef CHAT_H
#define CHAT_H
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "enc/algorithms/blowfish_crypt.h"
#include "utils/db_connector.h"
#include "dfs/types/headers/dfstruct.h"
#include <QDir>
#include <QDirIterator>
#include <QObject>

struct UIMessage
{
    QString messId;
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

struct ChatFileSender
{
    QString chatId;
    QString dfsName;
    QString originName;
    QString mime;
    int size;
};

class ChatManager;

class Chat : public QObject
{
    Q_OBJECT
private:
    QByteArray ownerID = "-1";
    QByteArray _chatId = "0";
    QByteArray _encryptionKey = "0";
    BigNumber _currentSession = 0; // temp
    QByteArray _currentActorId = "-1";
    AccountController* _accountController;
    ActorIndex* _actorIndex;
    ChatManager* _chatManager;

private:
    // paths getters:
    QString getPathCurrentChat();                   //+ keystore/chats/[chatId]/
    QString getPathToUsers();                       //+  keystore/chats/[chatId]/[sessionId]/users/
    QString pathToSession(BigNumber sessionNumber); //+  keystore/chats/[chatId]/[sessionId]
    // paths end
    BigNumber findCurrentSession();                                                     //+
    void InitializeAllPaths();                                                          //+
                                                                                        //+
    void createNewUsersDb(QList<QByteArray> userList, QList<QByteArray> userData = {}); //+
    bool isUserExist(QByteArray actorId, QList<QByteArray> userList);                   //+

public:
    Chat(ChatManager* chatManager, QByteArray chatId, ActorIndex* actorIndex,
         AccountController* accountController,
         BigNumber sessionNumb = -1); //+
    Chat(ChatManager* chatManager, QByteArray chatId, QByteArray key, BigNumber currentSession,
         ActorIndex* actorIndex, AccountController* accountController, QList<QByteArray> users,
         QByteArray _ownerId = "-1"); //+
    Chat(const Chat& tempChat);       //+
    ~Chat();
    bool isOwner();                                               //-
    bool isUserActual(QByteArray actorId, BigNumber sessionNumb); //-
    QByteArray unloadChatKey();                                   //+
    // QByteArray getChatPrivateKey();                               //+
    BigNumber getActualCurrentSession(); //+
    // BigNumber getMyCurrentSession();                              //
    bool createNewSession(QByteArray key, QList<QByteArray> users = {},
                          QByteArray _ownerId = "-1"); //+
    QByteArray sendMessage(QByteArray message);        //+
    // getters setters
    QByteArray getChatId() const;                    //+
    QByteArray getEncryptionKey() const;             //+
    BigNumber getSession();                          //+
    AccountController* getAccountController() const; //+
    void InviteNewUser(QByteArray actorId);          //+-
    bool isUserVerify(QByteArray actorId);           //?-

    QByteArray getCurrentActorId() const; //+
    QList<QByteArray> getAllUsers();      //+
    QList<UIMessage> getAllMessages();    //-
    ActorIndex* getActorIndex() const;    //+
    QByteArray getOwner();                //-
    QByteArray encryptByChatKey(QByteArray data);
    QByteArray decryptByChatKey(QByteArray data);
    UIMessage getLastMessage();
    void removeAllChatData();
    QByteArray encryptMessage(QByteArray message); //+
    QByteArray decryptMessage(QByteArray message);
    void saveChatKey(QByteArray key, BigNumber sessionNumb, QByteArray& _ownerId); //+
    void saveChatsId(const QByteArray& chatId);
    BigNumber getSessionConst() const;

signals:
    void sendDataToBlockchain(const QString& path); // send to blockchain. Connect with ChatManager
};

#endif // CHAT_MANAGER_H
