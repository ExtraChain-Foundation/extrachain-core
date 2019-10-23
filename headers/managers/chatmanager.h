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
    bool isValid(BigNumber chatId);     //+

public:
    ChatManager(AccountController *accController);                   //+
    void removeMemberFromChat(QByteArray chatId, BigNumber actorId); //-
    void addMemberToChat(QByteArray chatId, BigNumber actorId);      //+
    void CreateNewChat();                                            //+
    ~ChatManager();
public slots:
    void receiveInviteToChat(QByteArray chatId, QByteArray sessionNumb, QByteArray key); //+
signals:
    void sendInviteToChat(QByteArray chatId, QByteArray sessionNumb, BigNumber actorId, QByteArray key); //+
    void sendCreatedNewChat(QByteArray chatId);                                                          //+
};

#endif // CHATMANAGER_H
