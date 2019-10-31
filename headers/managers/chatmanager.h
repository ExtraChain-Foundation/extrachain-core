#ifndef CHATMANAGER_H
#define CHATMANAGER_H

#include "chat.h"
#include <QObject>
#include <QDirIterator>
#include <QList>
#include "dfs/types/headers/dfstruct.h"
// blockhain/index/actor/[myId]/myChats/[chatId]   /[chatId]+".dat"   file that consist reference to chat (as
// path) blockhain/index/actor/[myId]/myChats/[chatId]   /currentSession       file that consist current
// session for this chat blockhain/index/actor/[myId]/myChats/[chatId]   /keystore/key[SessionNumb]   //
// locale. Consist key for all chats

// blockhain/index/actor/[ownerId]/chatStorage/[chatId]  /users/[IdAddedUsers]     //files that contain sign
// of inviter and it id blockhain/index/actor/[ownerId]/chatStorage/[chatId]  /[sessionNumb]            //file
// that content user messages
class ChatManager : public QObject
{
    Q_OBJECT
private:
    AccountController *_accController;
    QList<Chat *> _chatList;
    QByteArray _actorPath; // blockhain/index/actors/
    QByteArray _currentActorId;

private:
    void InitializeChatList();                             //+
    void InitializeConnectSignalSlot();                    //+
    QByteArray convertChatIdToFullPath(QByteArray chatId); //+
    QByteArray generateChatId();                           //+
    QByteArray generateChatKey();                          //+

    QByteArray getPathToMyChats(); //+ blockhain/index/actors/[myId]/myChats/
                                   //  bool isUserVerify(QByteArray chatId, QByteArray actorId);
    // void createLocalChatFile(QByteArray chatId, QByteArray pathCreate, QByteArray chatPath); //?
public:
    ChatManager(AccountController *accController);                    //+
    void removeMemberFromChat(QByteArray chatId, QByteArray actorId); //+
    void CreateNewChat();                                             //+
    void InviteToChat(QByteArray chatId, QByteArray actorId);         //+
    ~ChatManager();                                                   //+
public slots:
    // void addedNewUserToChat(QByteArray chatId, QByteArray inviterId, QByteArray inviterSign,
    //                      QByteArray invitedId);
    void getSignalFromChats(const QString &path); //+ connect with Chats
signals:
    void sendDataToBlockhainFromChatManager(const QString &path,
                                            const based_dfs_struct::Type &type); //----- connet with dfs
};

#endif // CHATMANAGER_H
