#include "chatmanager.h"
/////////////////////////////////////////////////
///      D   E   S   C   R   I   B   E    ///////
/////////////////////////////////////////////////
// I) CREATE DIALOGUE:
// 1) Generate chat id.
// 2) Create mkpath to chat, keystore and users ( in chat constructor).
// 3) Generate key, encrypt it and place to mykeyStore
// 4) Create file with 0 session.
// 5) Create file with myId in users.
// 6) Invite user to chat

// II) INVITE TO CHAT:
// 1) Send message with current dialogue data to network.

// III) REMOVE MEMBER FROM CHAT:
// 1) Create new session (directory)
// 2) Create new session (file) in this directory
// 3) Create directory with users except user that was removed.
// 4) Send message with current dialogue data to network.
/////////////////////////////////////////////////
/// D   E   S   C   R   I   B   E      E   N   D/
/////////////////////////////////////////////////

// void ChatManager::createLocalChatFile(QByteArray chatId, QByteArray pathCreate, QByteArray chatPath)
//{
//    QDir().mkpath(pathCreate + chatStore + "myChats/");
//    QFile file(pathCreate + chatStore + "myChats/" + chatId);
//    if (file.open(QIODevice::WriteOnly))
//    {
//        file.write(chatPath);
//        file.close();
//        return;
//    }

//    qDebug() << "[Warning] File not open to read. Create local chat file method";

//    // emit signal to share chat file
//}

void ChatManager::InitializeChatList()
{
    QStringList chatList = QDir(getPathToMyChats()).entryList(QDir::Dirs);
    QByteArray chatPath = "-1";
    _chatList.clear();
    for (QString chat : chatList)
    {
        Chat *temp = new Chat(chat.toLocal8Bit(), _actorIndex, _accController);
        _chatList.push_front(temp);
    }
}

void ChatManager::InitializeConnectSignalSlot()
{
    //    foreach (Chat *currentChat, _chatList)
    //        connect(currentChat, &Chat::sendDataToBlockchain, this, &ChatManager::getSignalFromChats);
}

// QByteArray ChatManager::convertChatIdToFullPath(QByteArray chatId)
//{
//    QFile file(getPathToMyChats() + chatId + "/" + chatId + ".dat");
//    if (!file.exists())
//        return "-1";
//    if (file.open(QIODevice::ReadOnly))
//    {
//        QByteArray path = file.readLine();
//        file.close();
//        return path;
//    }
//    else
//        qDebug() << "[Warning] Cannot open file on write "
//                 << getPathToMyChats() + chatId + "/" + chatId + ".dat";
//    return "-1";
//}

QByteArray ChatManager::generateChatId()
{
    return generateChatKey();
}

QByteArray ChatManager::getPathToMyChats()
{
    return ChatStorage::STORED_CHATS;
}

QByteArray ChatManager::generateChatKey()
{
    return Utils::calcKeccak(BigNumber::random(64).toByteArray());
}

ChatManager::ChatManager(AccountController *accController, ActorIndex *actorIndex)
{
    this->_actorIndex = actorIndex;
    this->_accController = accController;
    QDir().mkpath(getPathToMyChats());
    InitializeChatList();
}

void ChatManager::removeMemberFromChat(QByteArray chatId, QByteArray actorId)
{
    BigNumber currentSession = Chat(chatId, _actorIndex, _accController).getActualCurrentSession();
    Chat temp = Chat(chatId, _actorIndex, _accController, currentSession + 1);
    if (!temp.isUserVerify(_currentActorId) || !temp.isOwner())
    {
        qDebug() << "[Warning] Can't invite to chat. User verify error, removeMemberFromChat. ChatManager";
        return;
    }

    if (temp.createNewSession(_accController->getCurrentActor().getKey()->encrypt(generateChatKey()),
                              temp.getAllUsers(), temp.getOwner()))
    {
        //        QFile file(getPathToMyChats() + chatId  + newSession);
        //        file.open(QIODevice::WriteOnly);
        //        file.close();

        // getSignalFromChats(convertChatIdToFullPath(chatId) + "sessions/" + newSession);

        QList<QByteArray> users = temp.getAllUsers();
        foreach (QByteArray currentUser, users)
        {
            if (temp.isUserActual(currentUser, temp.getSession()))
                InviteToChat(temp.getChatId(), actorId);
        }
    }
    else
        qDebug() << "[Error] when remove Member from chat. RemoveMemberFromChat ChatManager";
}

QByteArray ChatManager::CreateNewChat()
{
    QByteArray chatId = generateChatId();
    QDir().mkpath(getPathToMyChats() + chatId + "/");
    QByteArray key = _accController->getCurrentActor().getKey()->encrypt(generateChatKey());
    _chatList.push_front(new Chat(chatId, key, 0, _actorIndex, _accController,
                                  QList<QByteArray> { _currentActorId }, _currentActorId));
    return chatId;
    //    QDir().mkpath(getPathToMyChats() + chatId);
    //    QFile file(getPathToMyChats() + chatId + "/" + chatId + ".dat");
    //    if (file.open(QIODevice::WriteOnly))
    //    {
    //        file.write(_actorPath + _currentActorId + "/chatStorage/" + chatId + "/");
    //        file.close();
    //    }
}

void ChatManager::InviteToChat(QByteArray chatId, QByteArray actorId)
{
    Chat temp(chatId, _actorIndex, _accController);
    temp.InviteNewUser(actorId);
    //    if (!temp.isUserVerify(_currentActorId)
    //        || !temp.isUserActual(_currentActorId, temp.getActualCurrentSession()))
    //    {
    //        qDebug() << "[Warning] Can't invite to chat. User verify error, InviteToChat. ChatManager";
    //        return;
    //    }
    //    if (temp.isUserVerify(actorId))
    //        return;
    //    temp.InviteNewUser(KeyPublic(_actorIndex->getActor(BigNumber(actorId)).getKey()->getPublicKey())
    //                           .encrypt(temp.getChatPrivateKey()),
    //                       actorId);
}

void ChatManager::SendMessage(QByteArray chatId, QByteArray message)
{
    Chat temp(chatId, _actorIndex, _accController);
    temp.sendMessage(message);
}

void ChatManager::UIreceiveAllChats()
{
    emit UIsendAllChats(_chatList);
}

void ChatManager::createDialogue(QByteArray actorId)
{
    QList<UIChat> chats;
    QByteArray chatId = CreateNewChat();
    InviteToChat(chatId, actorId);
    QList<QByteArray> tempUsers;
    QStringList tempusersList;
    foreach (Chat *currentChat, _chatList)
        tempUsers = currentChat->getAllUsers();
    for (auto user : tempUsers)
        tempusersList.append(user);
    chats.append(UIChat { tempusersList, chatId });

    emit chatListSend(chats);
}

void ChatManager::requestChatList()
{
    QList<UIChat> chats;
    QList<QByteArray> tempUsers;
    QStringList tempusersList;
    foreach (Chat *currentChat, _chatList)
    {
        tempusersList.clear();
        tempUsers = currentChat->getAllUsers();
        for (auto user : tempUsers)
            tempusersList.append(user);
        chats.append(UIChat { tempusersList, currentChat->getChatId() });
    }
    emit chatListSend(chats);
}

void ChatManager::requestChat(QByteArray chatId)
{
    emit chatSend(chatId, Chat(chatId, _actorIndex, _accController).getAllMessages());
}

ChatManager::~ChatManager()
{
    _chatList.clear();
    // delete _accController;
}

void ChatManager::ActorInit()
{
    this->_currentActorId = this->_accController->getMainActor()->getId().toActorId();
    InitializeConnectSignalSlot();
}

// void ChatManager::addedNewUserToChat(QByteArray chatId, QByteArray inviterId, QByteArray inviterSign,
//                                     QByteArray invitedId)
//{
//    Chat tempChat(chatId, _accController);
//    tempChat.InviteNewUser(inviterId, inviterSign, invitedId);
//    if (!tempChat.isUserVerify(invitedId))
//        tempChat.removeUserFromChat(invitedId);
//}

// void ChatManager::getSignalFromChats(const QString &path)
//{
//    emit sendDataToBlockhainFromChatManager(path, based_dfs_struct::Type::chates);
//}
