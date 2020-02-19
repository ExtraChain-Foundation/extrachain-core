#include "managers/chatmanager.h"
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

void ChatManager::setNetManager(NetManager *value)
{
    netManager = value;
}

void ChatManager::AddChat(QByteArray chatId, QByteArray key, QByteArray owner)
{

    QDir().mkpath(getPathToMyChats() + chatId + "/");
    _chatList.push_front(new Chat(this, chatId, key, 0, _actorIndex, _accController,
                                  QList<QByteArray> { owner, _currentActorId }, owner));
    return;
    QString path(ChatStorage::STORED_CHATS + owner + "/chats/" + chatId);
    emit requestFile(path + "/0/msg");
    emit requestFile(path + "/users");
    emit requestFile(path + "/0/msg.stored");
    emit requestFile(path + "/users.stored");
    //    QString pathUser = chatId + "/users";
    //    QString pathMsg = chatId + "/0/msg";
    //    emit send(DfsStruct::DfsSave::Static, pathUser, "", DfsStruct::chat, DfsStruct::SubType::undef);
    //    emit send(DfsStruct::DfsSave::Static, pathMsg, "", DfsStruct::chat, DfsStruct::SubType::undef);
}

void ChatManager::InitializeChatList()
{
    //    QStringList chatList = QDir(getPathToMyChats()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    _chatList.clear();
    QString filePath = "data/" + _currentActorId + "/private/chats";

    if (!QFile::exists(filePath))
        return;

    DBConnector DB(filePath.toStdString());
    std::vector<DBRow> chats = DB.select("SELECT * FROM " + Config::DataStorage::chatIdTableName);
    for (DBRow temp : chats)
    {
        Chat *temp_ = new Chat(this, QByteArray::fromStdString(temp["chatId"]), _actorIndex, _accController);
        _chatList.push_front(temp_);
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
    return ChatStorage::STORED_CHATS + _currentActorId + "/chats/";
}

void ChatManager::parseInvite()
{
    QByteArray path = "data/" + _currentActorId + "/services/chatinvite";
    if (!QFile::exists(path))
        return;

    DBConnector db;
    db.open(path.toStdString());
    std::vector<DBRow> invites = db.select("SELECT * from Invite");

    for (const auto &invite : invites)
    {
        QByteArray owner =
            /*_accController->getMainActor()->getKey()->decrypt(*/ QByteArray::fromStdString(
                invite.at("owner")) /*)*/;
        QByteArray chatId = QByteArray::fromStdString(invite.at("chatId"));
        QByteArray key = QByteArray::fromStdString(invite.at("message"));

        if (owner.length() != 20 || !BigNumber::isValid(owner))
        {
            continue;
        }

        Chat temp(this, chatId, _actorIndex, _accController);
        temp.saveChatKey(_accController->getMainActor()->getKey()->decrypt(key), BigNumber(0), owner);
        // temp.sendMessage("{ \"type\": \"first\" }");

        // AddChat(chatId, _accController->getMainActor()->getKey()->decrypt(key), owner);
        //        QList<QByteArray> allUsers = temp.getAllUsers();
        //        QStringList tempusersList;

        // for (auto user : allUsers)
        //      tempusersList.append(user);
        //        tempusersList.append(_currentActorId);
        //        tempusersList.append(owner);
        // emit chatCreated(
        //    UIChat { tempusersList, chatId, Chat(chatId, _actorIndex, _accController).getLastMessage() });

        //        QByteArray pathToUsersFile = ChatStorage::STORED_CHATS + owner + "/chats/" + chatId +
        //        "/users"; QByteArray pathToMsgFile = ChatStorage::STORED_CHATS + owner + "/chats/" + chatId
        //        + "/0/msg"; QByteArray pathToUsersFileStored =
        //            ChatStorage::STORED_CHATS + owner + "/chats/" + chatId + "/users.stored";
        //        QByteArray pathToMsgFileStored =
        //            ChatStorage::STORED_CHATS + owner + "/chats/" + chatId + "/0/msg.stored";

        //        emit requestFile(pathToUsersFile);
        //        emit requestFile(pathToMsgFile);
        //        emit requestFile(pathToUsersFileStored);
        //        emit requestFile(pathToMsgFileStored);

        sendEditSql(_currentActorId, "chatinvite", DfsStruct::Type::Service, DfsStruct::ChangeType::Delete,
                    { "Invite", "chatId", chatId });
        emit newNotify({ QDateTime::currentMSecsSinceEpoch(), notification::NotifyType::ChatInvite,
                         owner + " " + chatId });
        requestChatList();
    }
}

QMap<QByteArray, QByteArray> ChatManager::extractChatKey()
{
    QMap<QByteArray, QByteArray> chatKey;
    foreach (Chat *currentChat, _chatList)
        chatKey[currentChat->getChatId()] = currentChat->getEncryptionKey();
    return chatKey;
}

QByteArray ChatManager::generateChatKey()
{
    return Utils::calcKeccak(BigNumber::random(65).toByteArray());
}

ChatManager::ChatManager(AccountController *accController, ActorIndex *actorIndex)
{
    this->_actorIndex = actorIndex;
    this->_accController = accController;
    // QDir().mkpath(getPathToMyChats());
    // QDir().mkpath(ChatStorage::KEYSTORE_CHATS.c_str());
    // InitializeChatList();
}

void ChatManager::msgReceiver(const Messages::BaseMessage &msg)
{
    return;
    //
    //    if (msg.getMsgType() == Messages::INVITE_CHAT_MESSAGE)
    //    {
    //        InviteChatMessages message(msg.getData());
    //        QByteArray owner = _accController->getMainActor()->getKey()->decrypt(message.owner);

    //        if (owner.length() != 20 || !BigNumber::isValid(owner))
    //        {
    //            netManager->sendMessage(msg.serialize());
    //            return;
    //        }

    //        if (owner == msg.getSigner())
    //        {
    //            AddChat(message.id, _accController->getMainActor()->getKey()->decrypt(message.key), owner);
    //            QList<QByteArray> allUsers = { owner, _currentActorId };
    //            QStringList tempusersList;

    //            Chat temp(message.id, _actorIndex, _accController);
    //            temp.sendMessage("{ \"type\": \"first\" }");

    //            for (auto user : allUsers)
    //                tempusersList.append(user);
    //            emit chatCreated(UIChat { tempusersList, message.id,
    //                                      Chat(message.id, _actorIndex, _accController).getLastMessage() });
    //        }
    //        else
    //            netManager->sendMessage(msg.serialize());
    //    }
}

bool ChatManager::isChatExist(QByteArray chatId)
{
    return QDir(getPathToMyChats() + chatId).exists();
}

void ChatManager::removeMemberFromChat(QByteArray chatId, QByteArray actorId)
{
    BigNumber currentSession = Chat(this, chatId, _actorIndex, _accController).getActualCurrentSession();
    Chat temp = Chat(this, chatId, _actorIndex, _accController, currentSession + 1);
    if (!temp.isUserVerify(_currentActorId) || !temp.isOwner())
    {
        qDebug() << "[Warning] Can't invite to chat. User verify error, removeMemberFromChat. ChatManager";
        return;
    }

    if (temp.createNewSession(_accController->getMainActor()->getKey()->encrypt(generateChatKey()),
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
    _chatList.push_front(new Chat(this, chatId, generateChatKey(), 0, _actorIndex, _accController,
                                  QList<QByteArray> { _currentActorId }, _currentActorId));
    // Chat initialize
    QList<QByteArray> allUsers = Chat(this, chatId, _actorIndex, _accController).getAllUsers();

    return chatId;
}

void ChatManager::InviteToChat(QByteArray chatId, QByteArray actorId)
{
    Chat temp(this, chatId, _actorIndex, _accController);
    temp.InviteNewUser(actorId);
    InviteChatMessages msg;
    msg.id = chatId;
    msg.key = _actorIndex->getActor(BigNumber(actorId)).getKey()->encrypt(temp.unloadChatKey());
    msg.owner =
        _currentActorId; // _actorIndex->getActor(BigNumber(actorId)).getKey()->encrypt(_currentActorId);
                         // // encrypt

    QByteArrayList query = { "Invite", "chatId", msg.id, "message", msg.key, "owner", msg.owner };
    sendEditSql(actorId, "chatinvite", DfsStruct::Type::Service, DfsStruct::ChangeType::Insert, query);
    // sendMessage(msg.serialize(), Messages::INVITE_CHAT_MESSAGE);
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

void ChatManager::sendChatFile(QByteArray chatId, QString filePath)
{
    Chat temp(this, chatId, _actorIndex, _accController);

    QString fileName = QUrl(filePath).toLocalFile();

    QFile file(fileName);
    if (!file.exists())
    {
        qDebug() << "Cant send file";
        return;
    }

    QString newFileName =
        chatId + "/" + temp.getSession().toByteArray() + "/" + QFileInfo(fileName).fileName();
    QString newFileNameData = "data/" + temp.getOwner() + "/chats/" + newFileName;
    if (!file.copy(newFileNameData))
    {
        qDebug() << "Cant copy file";
        return;
    }

    emit send(DfsStruct::DfsSave::StaticNonStored, newFileName, "", DfsStruct::Chat);

    QByteArray message = "{ \"type\":\"file\",\"message\":\"" + newFileNameData.toLatin1() + "\"}";
    qDebug() << message;
    qint64 messId = QDateTime::currentMSecsSinceEpoch() + QRandomGenerator::global()->bounded(100);
    emit sendEditSql(
        temp.getOwner(), chatId + "/" + temp.getSession().toByteArray() + "/" + "msg", DfsStruct::Type::Chat,
        DfsStruct::ChangeType::Insert,
        { Config::DataStorage::chatMessageTableName.c_str(), "messId", QByteArray::number(messId), "userId",
          _currentActorId, "message", temp.encryptMessage(message), "type", "blob", "session",
          temp.getSession().toByteArray(), "date", QByteArray::number(QDateTime::currentMSecsSinceEpoch()) });
}

void ChatManager::SendMessage(QByteArray chatId, QByteArray message)
{
    Chat temp(this, chatId, _actorIndex, _accController);
    qint64 messId = QDateTime::currentMSecsSinceEpoch() + QRandomGenerator::global()->bounded(100);

    sendEditSql(temp.getOwner(), chatId + "/" + temp.getSession().toByteArray() + "/" + "msg",
                DfsStruct::Type::Chat, DfsStruct::ChangeType::Insert,
                { Config::DataStorage::chatMessageTableName.c_str(), "messId", QByteArray::number(messId),
                  "userId", _currentActorId, "message", temp.encryptMessage(message), "type", "blob",
                  "session", temp.getSession().toByteArray(), "date",
                  QByteArray::number(QDateTime::currentMSecsSinceEpoch()) });
}

void ChatManager::createDialogue(QByteArray actorId)
{
    QList<UIChat> chats;
    QByteArray chatId = CreateNewChat();
    InviteToChat(chatId, actorId);

    QList<QByteArray> allUsers = Chat(this, chatId, _actorIndex, _accController).getAllUsers();
    QStringList tempusersList;

    Chat temp(this, chatId, _actorIndex, _accController);
    temp.sendMessage("{ \"type\": \"first\" }");

    // SAVE TO DFS
    QString pathUser = temp.getChatId() + "/users";
    QString pathMsg = temp.getChatId() + "/" + temp.getSession().toByteArray() + "/msg";

    emit send(DfsStruct::DfsSave::Static, pathUser, "", DfsStruct::Chat);
    emit send(DfsStruct::DfsSave::Static, pathMsg, "", DfsStruct::Chat);

    for (auto &user : allUsers)
        tempusersList.append(user);
    //    emit chatCreated(
    //        UIChat { tempusersList, chatId, Chat(chatId, _actorIndex, _accController).getLastMessage() });
}

void ChatManager::requestChatList()
{
    InitializeChatList();
    QList<UIChat> chats;
    QList<QByteArray> tempUsers;
    QStringList tempusersList;
    foreach (Chat *currentChat, _chatList)
    {
        tempusersList.clear();
        tempUsers = currentChat->getAllUsers();
        for (auto user : tempUsers)
            tempusersList.append(user);

        chats.append(UIChat { tempusersList, currentChat->getChatId(), currentChat->getLastMessage() });
    }
    emit chatListSend(chats);
}

void ChatManager::requestChat(QByteArray chatId)
{
    emit chatSend(chatId, Chat(this, chatId, _actorIndex, _accController).getAllMessages());
}

void ChatManager::chatRemoved(QByteArray chatId)
{
    Chat(this, chatId, _actorIndex, _accController).removeAllChatData();
    int i = 0;
    for (auto currentchat : _chatList)
    {
        if (currentchat->getChatId() == chatId)
            _chatList.removeAt(i);
        i++;
    }
}

void ChatManager::changes(QString path)
{
    if (path.contains(".stored"))
        return;
    if (path.contains("chatinvite"))
    {
        parseInvite();
        return;
    }
    else if (path.contains("follower") && path.contains(_currentActorId))
    {
        DBConnector db(path.toStdString());

        std::vector<DBRow> res = db.select("SELECT * FROM " + Config::DataStorage::subscribeFollowerTableName
                                           + " ORDER BY subscriber LIMIT 1");
        if (res.size() != 1)
            return;
        QByteArray userId = res[0].at("subscriber").c_str();
        emit newNotify(
            { QDateTime::currentMSecsSinceEpoch(), notification::NotifyType::NewFollower, userId });
    }
    else if (path.contains("chat"))
    {
        DBConnector db(path.toStdString());

        std::vector<DBRow> res = db.select("SELECT * FROM " + Config::DataStorage::chatMessageTableName
                                           + " ORDER BY date DESC LIMIT 1");
        if (res.size() != 1)
            return;

        QByteArray userId = res[0].at("userId").c_str();

        QString chatID = path.mid(32, 64);
        QFile file("keystore/chats/" + _currentActorId + "/fileChatsId");
        file.open(QIODevice::ReadOnly);
        QByteArray data = file.readAll();
        file.close();
        QByteArrayList chatsId = Serialization::universalDeserialize(data, 4);
        if (!chatsId.contains(chatID.toUtf8()))
            return;
        Chat tmp(this, chatID.toUtf8(), _actorIndex, _accController);
        if (userId != _currentActorId)
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(), notification::NotifyType::ChatMsg,
                             userId + " " + chatID.toUtf8() });

        auto allMessage = tmp.getAllMessages();
        emit sendLastMessage(chatID.toUtf8(), allMessage.takeLast());
        emit chatSend(chatID.toUtf8(), tmp.getAllMessages());
    }
    //    QDateTime currentDate = QDateTime::fromMSecsSinceEpoch(std::stol(res[0]["date"]));
}

void ChatManager::process()
{
}

void ChatManager::fileLoaded(const QString &path)
{
    qDebug() << path;
    if (path.right(5) == "users")
    {
        requestChatList();
    }
    else if (path.right(3) == "msg")
    {
        /*
        DBConnector db(path.toStdString());
        std::vector<DBRow> res = db.select("SELECT * FROM " + Config::DataStorage::chatMessageTableName
                                           + " ORDER BY date DESC LIMIT 1");
        if (res.size() != 1)
            return;

        QString chatID = path.mid(32, 64);
        Chat tmp(chatID.toUtf8(), _actorIndex, _accController);
        QDateTime currentDate = QDateTime::fromMSecsSinceEpoch(std::stol(res[0]["date"]));
        emit sendLastMessage(chatID.toUtf8(),
                             UIMessage { res[0]["userId"].c_str(),
                                         tmp.decryptMessage(QByteArray::fromStdString(res[0]["message"])),
                                         currentDate });
        */
        requestChatList();
    }
    if (path.indexOf("chatinvite") != -1 && path.indexOf(".stored") == -1)
        parseInvite();
}

void ChatManager::initChat(bool status, int type)
{
    Q_UNUSED(status)

    if (type == 1)
        return;

    QTimer::singleShot(5000, [&]() {
        parseInvite();
        QByteArray pathToChatInvite =
            ChatStorage::STORED_CHATS + _currentActorId + "/services/chatinvite.stored";
        emit requestFile(pathToChatInvite);
        QString filePath = "data/" + _currentActorId + "/private/chats";

        if (!QFile::exists(filePath))
            return;

        DBConnector DB(filePath.toStdString());
        std::vector<DBRow> chats = DB.select("SELECT * FROM " + Config::DataStorage::chatIdTableName);

        for (DBRow &tmp : chats)
        {
            QByteArray owner = tmp["owner"].c_str();
            QByteArray chatId = tmp["chatId"].c_str();
            QByteArray pathToUsersFile = ChatStorage::STORED_CHATS + owner + "/chats/" + chatId + "/users";
            QByteArray pathToMsgFile = ChatStorage::STORED_CHATS + owner + "/chats/" + chatId + "/0/msg";
            QByteArray pathToUsersFileStored =
                ChatStorage::STORED_CHATS + owner + "/chats/" + chatId + "/users.stored";
            QByteArray pathToMsgFileStored =
                ChatStorage::STORED_CHATS + owner + "/chats/" + chatId + "/0/msg.stored";

            emit requestFile(pathToUsersFile);
            emit requestFile(pathToMsgFile);
            emit requestFile(pathToUsersFileStored);
            emit requestFile(pathToMsgFileStored);
        }
    });
}

ChatManager::~ChatManager()
{
    _chatList.clear();
    // delete _accController;
}

void ChatManager::ActorInit()
{
    this->_currentActorId = this->_accController->getMainActor()->getId().toActorId();
    QFile file("keystore/personal/currentID");
    file.open(QIODevice::ReadWrite);
    file.resize(0);
    file.write(this->_currentActorId);
    file.close();
    InitializeConnectSignalSlot();
    InitializeChatList();
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

const QByteArray InviteChatMessages::serialize()
{
    return Serialization::universalSerialize({ id, owner, key });
}

InviteChatMessages::InviteChatMessages()
{
}

InviteChatMessages::InviteChatMessages(const QByteArray &serialized)
{
    QByteArrayList data = Serialization::universalDeserialize(serialized);
    if (data.size() != 3)
    {
        qDebug() << "Invalid struct ChatMessager";
        return;
    }
    id = data.takeFirst();
    owner = data.takeFirst();
    key = data.takeFirst();
}
const QByteArray ChatMessage::serialize()
{
    return Serialization::universalSerialize({ id, senderMsg, message, salt });
}

ChatMessage::ChatMessage()
{
}

ChatMessage::ChatMessage(const QByteArray &serialized)
{
    QByteArrayList data = Serialization::universalDeserialize(serialized);
    if (data.size() != 4)
    {
        qDebug() << "Invalid struct ChatMessager";
        return;
    }
    id = data.takeFirst();
    senderMsg = data.takeFirst();
    message = data.takeFirst();
    salt = data.takeFirst();
}
