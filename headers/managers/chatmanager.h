#ifndef CHATMANAGER_H
#define CHATMANAGER_H

#include "chat.h"
#include <QObject>
#include <QDirIterator>
#include <QList>
class ChatManager : public QObject
{
    Q_OBJECT
private:
    AccountController *_accController;
    QList<Chat> _chatList;

private:
    void Initialize();                  //+
    void InitializeConnectSignalSlot(); //+
    QByteArray generateChatId();        //+
    QByteArray generateChatKey();       //+
    bool isValid(QByteArray chatId);    //+
    bool isUserVerify(QByteArray chatId, QByteArray actorId);

public:
    ChatManager(AccountController *accController);                    //+
    void removeMemberFromChat(QByteArray chatId, QByteArray actorId); //-
    void addMemberToChat(QByteArray chatId, QByteArray actorId);      //+
    void CreateNewChat();                                             //+
    ~ChatManager();
public slots:
    void receiveInviteToChat(QByteArray chatId, QByteArray sessionNumb, QByteArray key); //+
    void addedNewUserToChat(QByteArray chatId, QByteArray inviterId, QByteArray inviterSign,
                            QByteArray invitedId);
signals:
    void sendInviteToChat(QByteArray chatId, QByteArray sessionNumb, QByteArray actorId, QByteArray key); //+
    void sendCreatedNewChat(QByteArray chatId);                                                           //+
};

#endif // CHATMANAGER_H
