/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef CHATMANAGER_H
#define CHATMANAGER_H

#include "chat.h"
#include <QObject>
#include <QList>
#include <QTimer>
#include "dfs/types/headers/dfstruct.h"
#include "utils/db_connector.h"
// blockhain/index/actor/[myId]/myChats/[chatId]   /[chatId]+".dat"   file that consist reference to chat (as
// path) blockhain/index/actor/[myId]/myChats/[chatId]   /currentSession       file that consist current
// session for this chat blockhain/index/actor/[myId]/myChats/[chatId]   /keystore/key[SessionNumb]   //
// locale. Consist key for all chats

// blockhain/index/actor/[ownerId]/chatStorage/[chatId]  /users/[IdAddedUsers]     //files that contain sign
// of inviter and it id blockhain/index/actor/[ownerId]/chatStorage/[chatId]  /[sessionNumb]            //file
// that content user messages

#ifndef NETWORK_MANAGER_DEF
#define NETWORK_MANAGER_DEF
class NetManager;
#include "network/network_manager.h"
#endif

class ChatManager : public QObject
{
    Q_OBJECT
private:
    const QByteArray _salt = "invitetochat";
    AccountController *_accController;
    ActorIndex *_actorIndex;
    QList<Chat *> _chatList;
    QByteArray _currentActorId;
    NetManager *netManager;

private:
    void AddChat(QByteArray chatId, QByteArray key, QByteArray owner);
    void InitializeChatList();          //+
    void InitializeConnectSignalSlot(); //-
    QByteArray generateChatId();        //+
    QByteArray generateChatKey();       //+
    QString getPathToMyChats();         //+ keystore/chats/
    void parseInvite();
    // bool isUserVerify(QByteArray chatId, QByteArray actorId);
    // void createLocalChatFile(QByteArray chatId, QByteArray pathCreate, QByteArray chatPath); //?
    // QByteArray convertChatIdToFullPath(QByteArray chatId); //

public:
    ChatManager(AccountController *accController, ActorIndex *actorIndex); //+
    void msgReceiver(const Messages::BaseMessage &msg);
    bool isChatExist(QByteArray chatId);
    ~ChatManager(); //+

    void setNetManager(NetManager *value);
    QMap<QByteArray, QByteArray> extractChatKey();

public slots:
    void ActorInit(); //+
    // void addedNewUserToChat(QByteArray chatId, QByteArray inviterId, QByteArray inviterSign,
    //                      QByteArray invitedId);
    //    void getSignalFromChats(const QString &path);                     // connect with Chats
    void removeMemberFromChat(QByteArray chatId, QByteArray actorId); //+
    QByteArray CreateNewChat();                                       //+

    void InviteToChat(QByteArray chatId, QByteArray actorId); //+
    void sendChatFile(ChatFileSender chatFile);
    void SendMessage(QByteArray chatId, QByteArray message, QString type); //+
    void removeChatMessage(QString chatId, QString messId);
    void createDialogue(QByteArray actorId); //+
    void requestChatList();                  //+
    void requestChat(QByteArray chatId);     //-
    void chatRemoved(QByteArray chatId);
    void changes(QString path, DfsStruct::ChangeType changeType);
    void process();
    void fileLoaded(const QString &path);
    void initChat(bool status, int type);
    Chat *getChatMemory(QByteArray chatId);

signals:
    void newNotify(const Notification ntf);
    void UIsendAllChats(QList<Chat *> chatList); // need connect to UI
    void sendDataToBlockhainFromChatManager(const QString &path,
                                            const DfsStruct::Type &type); //----- connet with dfs

    void chatListSend(QList<UIChat> chats);
    void chatSend(QByteArray chatId, QList<UIMessage> messages);
    void sendMessage(const QByteArray &data, const unsigned int &type);

    void sendLastMessage(QByteArray chatId, UIMessage); // from network & local send
    void chatCreated(UIChat);
    void finished();
    void sendEditSql(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                     QByteArrayList sqlChanges);
    void send(DfsStruct::DfsSave saveType, QString file, QByteArray data, const DfsStruct::Type type);
    void requestFile(const QString &filePath, const SocketPair &receiver = SocketPair());
};

#endif // CHATMANAGER_H

/*
    get chat list
    // chatId
    // actorId, если диалог
    // chatType = 1 - диалог, 2 - группа
    // last message
    // last message time


*/
