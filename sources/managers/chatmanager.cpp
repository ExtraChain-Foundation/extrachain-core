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
    QString path(DfsStruct::ROOT_FOOLDER_NAME + "/" + owner + "/chats/" + chatId);
    emit requestFile(path + "/0/msg");
    emit requestFile(path + "/users");
    emit requestFile(path + "/0/msg" + DfsStruct::STORED_EXT);
    emit requestFile(path + "/users" + DfsStruct::STORED_EXT);
    //    QString pathUser = chatId + "/users";
    //    QString pathMsg = chatId + "/0/msg";
    //    emit send(DfsStruct::DfsSave::Static, pathUser, "", DfsStruct::chat, DfsStruct::SubType::undef);
    //    emit send(DfsStruct::DfsSave::Static, pathMsg, "", DfsStruct::chat, DfsStruct::SubType::undef);
}

void ChatManager::InitializeChatList()
{
    //    QStringList chatList = QDir(getPathToMyChats()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    // _chatList.clear();
    QString filePath = DfsStruct::ROOT_FOOLDER_NAME + "/" + _currentActorId + "/private/chats";

    if (!QFile::exists(filePath))
        return;

    DBConnector DB(filePath.toStdString());
    std::vector<DBRow> chats = DB.select("SELECT * FROM " + Config::DataStorage::chatIdTableName);
    for (DBRow temp : chats)
    {
        QByteArray chatId = _accController->getMainActor()->key()->decryptSymmetric(
            QByteArray::fromStdString(temp["chatId"]));

        Chat *chat = getChatMemory(chatId);
        if (chat == nullptr)
        {
            Chat *temp_ = new Chat(this, chatId, _actorIndex, _accController);
            _chatList.push_front(temp_);
        }
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

QString ChatManager::getPathToMyChats()
{
    return DfsStruct::ROOT_FOOLDER_NAME + "/" + _currentActorId + "/chats/";
}

void ChatManager::parseInvite()
{
    QString path = DfsStruct::ROOT_FOOLDER_NAME + "/" + _currentActorId + "/services/chatinvite";
    if (!QFile::exists(path))
        return;

    auto mainActor = _accController->getMainActor()->key();
    DBConnector db;
    db.open(path.toStdString());
    std::vector<DBRow> invites = db.select("SELECT * from " + Config::DataStorage::chatInviteTableName);

    for (const auto &invite : invites)
    {
        QByteArray owner = mainActor->decrypt(QByteArray::fromStdString(invite.at("owner")));
        QByteArray chatIdEncrypted = QByteArray::fromStdString(invite.at("chatId"));
        QByteArray chatId = mainActor->decrypt(chatIdEncrypted);
        QByteArray key = mainActor->decrypt(QByteArray::fromStdString(invite.at("message")));

        if (owner.length() != 20 || !BigNumber::isValid(owner))
            continue;

        qDebug() << "invite" << owner << chatId << key;
        Chat temp(this, chatId, _actorIndex, _accController);
        temp.saveChatKey(key, BigNumber(0), owner);
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

        //        QByteArray pathToUsersFile = DfsStruct::ROOT_FOOLDER_NAME + "/" + owner + "/chats/" + chatId
        //        +
        //        "/users"; QByteArray pathToMsgFile = DfsStruct::ROOT_FOOLDER_NAME + "/" + owner + "/chats/"
        //        + chatId
        //        + "/0/msg"; QByteArray pathToUsersFileStored =
        //            DfsStruct::ROOT_FOOLDER_NAME + "/" + owner + "/chats/" + chatId + "/users" +
        //            DfsStruct::STORED_EXT;
        //        QByteArray pathToMsgFileStored =
        //            DfsStruct::ROOT_FOOLDER_NAME + "/" + owner + "/chats/" + chatId + "/0/msg" +
        //            DfsStruct::STORED_EXT;

        //        emit requestFile(pathToUsersFile);
        //        emit requestFile(pathToMsgFile);
        //        emit requestFile(pathToUsersFileStored);
        //        emit requestFile(pathToMsgFileStored);

        sendEditSql(_currentActorId, "chatinvite", DfsStruct::Type::Service, DfsStruct::ChangeType::Delete,
                    { Config::DataStorage::chatInviteTableName.c_str(), "chatId", chatIdEncrypted });
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

    if (temp.createNewSession(_accController->getMainActor()->key()->encrypt(generateChatKey()),
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
    Chat *chat = new Chat(this, chatId, generateChatKey(), 0, _actorIndex, _accController,
                          QList<QByteArray> { _currentActorId }, _currentActorId);
    _chatList.push_front(chat);
    // Chat initialize
    QList<QByteArray> allUsers = chat->getAllUsers();

    return chatId;
}

void ChatManager::InviteToChat(QByteArray chatId, QByteArray actorId)
{
    auto actor = _actorIndex->getActor(BigNumber(actorId));
    auto key = actor.key();

    Chat *chat = getChatMemory(chatId);
    if (chat == nullptr)
    {
        qDebug() << "[InviteToChat] Error loading exists chat";
        return;
    }
    chat->InviteNewUser(actorId);

    QByteArrayList query = { Config::DataStorage::chatInviteTableName.c_str(),
                             "chatId",
                             key->encrypt(chatId),
                             "message",
                             key->encrypt(chat->unloadChatKey()),
                             "owner",
                             key->encrypt(_currentActorId) };
    sendEditSql(actorId, "chatinvite", DfsStruct::Type::Service, DfsStruct::ChangeType::Insert, query);
}

void ChatManager::sendChatFile(ChatFileSender chatFile)
{
    QJsonObject dataObj;
    dataObj["name"] = chatFile.originName;
    dataObj["size"] = chatFile.size;
    dataObj["mime"] = chatFile.mime;

    QJsonObject jsonObj;
    jsonObj["type"] = "file";
    jsonObj["message"] = chatFile.dfsName;
    jsonObj["data"] = QString(QJsonDocument(dataObj).toJson(QJsonDocument::Compact));

    QByteArray message = QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);
    SendMessage(chatFile.chatId.toLatin1(), message, "file");
}

void ChatManager::SendMessage(QByteArray chatId, QByteArray message, QString type)
{
    auto chat = getChatMemory(chatId);
    auto messId = QDateTime::currentMSecsSinceEpoch() + QRandomGenerator::global()->bounded(100);
    auto owner = chat->getOwner();
    auto session = chat->getSession().toByteArray();
    auto date = QByteArray::number(QDateTime::currentMSecsSinceEpoch());

    auto encryptedMessageId = chat->encryptMessage(QByteArray::number(messId));
    auto encryptedActorId = chat->encryptMessage(_currentActorId);
    auto encryptedMessage = chat->encryptMessage(message);
    auto encryptedType = chat->encryptMessage(type.toLatin1());
    auto encryptedSession = chat->encryptMessage(chat->getSession().toByteArray());

    sendEditSql(owner, chatId + "/" + session + "/" + "msg", DfsStruct::Type::Chat,
                DfsStruct::ChangeType::Insert,
                { Config::DataStorage::chatMessageTableName.c_str(), "messId", encryptedMessageId, "userId",
                  encryptedActorId, "message", encryptedMessage, "type", encryptedType, "session",
                  encryptedSession, "date", date });
}

void ChatManager::removeChatMessage(QString chatId, QString messId)
{
    Chat *chat = getChatMemory(chatId.toLatin1());
    auto owner = chat->getOwner();
    auto session = chat->getSession().toByteArray();
    auto encryptedMessageId = chat->encryptMessage(messId.toLatin1());
    qDebug() << "CR123" << chatId << owner << session << encryptedMessageId;

    sendEditSql(owner, chatId + "/" + session + "/" + "msg", DfsStruct::Type::Chat,
                DfsStruct::ChangeType::Delete,
                { Config::DataStorage::chatMessageTableName.c_str(), "messId", encryptedMessageId });
}

void ChatManager::createDialogue(QByteArray actorId)
{
    QList<UIChat> chats;
    QByteArray chatId = CreateNewChat();
    InviteToChat(chatId, actorId);

    Chat *chat = getChatMemory(chatId);
    QList<QByteArray> allUsers = chat->getAllUsers();
    QStringList tempusersList;

    chat->sendMessage("{ \"type\": \"first\" }");

    // SAVE TO DFS
    QString pathUser = chat->getChatId() + "/users";
    QString pathMsg = chat->getChatId() + "/" + chat->getSession().toByteArray() + "/msg";

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
    emit chatSend(chatId, getChatMemory(chatId)->getAllMessages());
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

void ChatManager::changes(QString path, DfsStruct::ChangeType changeType)
{
    if (path.contains(DfsStruct::STORED_EXT))
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
            { QDateTime::currentMSecsSinceEpoch(), Notification::NotifyType::NewFollower, userId });
    }
    else if (path.contains(QString("%1/private/chats").arg(QString(_currentActorId))))
    {
        qDebug() << "Chat list updated";
        // TODO!: update chats file
        requestChatList();
    }
    else if (path.contains("chats") && path.contains("msg"))
    {
        DBConnector db(path.toStdString());

        std::vector<DBRow> res = db.select("SELECT * FROM " + Config::DataStorage::chatMessageTableName
                                           + " ORDER BY date DESC LIMIT 1");
        if (res.size() != 1)
            return;

        QString chatId = path.mid(32, 64);
        Chat *chat = getChatMemory(chatId.toLatin1());
        QByteArray userId = chat->decryptMessage(QByteArray::fromStdString(res[0]["userId"]));

        /*
        QFile file("keystore/chats/" + _currentActorId + "/fileChatsId");
        file.open(QIODevice::ReadOnly);
        QByteArray data = file.readAll();
        file.close();
        QByteArrayList chatsId = Serialization::deserialize(data, 4);
        */
        bool myChat = false;
        for (auto chat : _chatList)
        {
            if (chat->getChatId() == chatId.toLatin1())
            {
                myChat = true;
                break;
            }
        }
        if (!myChat)
            return;

        Chat *tmp = getChatMemory(chatId.toLatin1());
        if (tmp == nullptr)
            return;
        if (userId != _currentActorId && changeType == DfsStruct::ChangeType::Insert)
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(), Notification::NotifyType::ChatMsg,
                             userId + " " + chatId.toUtf8() });

        auto allMessage = tmp->getAllMessages();
        if (allMessage.length() > 0)
        {
            if (changeType == DfsStruct::ChangeType::Insert)
                emit sendLastMessage(chatId.toUtf8(), allMessage.takeLast());
            else if (changeType == DfsStruct::ChangeType::Delete)
                emit chatSend(chatId.toUtf8(), tmp->getAllMessages());
        }
    }
    // QDateTime currentDate = QDateTime::fromMSecsSinceEpoch(std::stol(res[0]["date"]));
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
    if (path.indexOf("chatinvite") != -1 && path.indexOf(DfsStruct::STORED_EXT) == -1)
        parseInvite();
}

void ChatManager::initChat(bool status, int type)
{
    Q_UNUSED(status)

    if (type == 1)
        return;

    /*
    QString pathToChatInvite = DfsStruct::ROOT_FOOLDER_NAME + "/" + _currentActorId + "/services/chatinvite";
    if (QFile::exists(pathToChatInvite))
        parseInvite();
    return;
    */

    QTimer::singleShot(5000, [&]() {
        parseInvite();
        QString pathToChatInvite = DfsStruct::ROOT_FOOLDER_NAME + "/" + _currentActorId
            + "/services/chatinvite" + DfsStruct::STORED_EXT;
        emit requestFile(pathToChatInvite);
        QString filePath = DfsStruct::ROOT_FOOLDER_NAME + "/" + _currentActorId + "/private/chats";

        if (!QFile::exists(filePath))
            return;

        DBConnector DB(filePath.toStdString());
        std::vector<DBRow> chats = DB.select("SELECT * FROM " + Config::DataStorage::chatIdTableName);
        auto mainActor = _accController->getMainActor()->key();

        for (DBRow &tmp : chats)
        {
            QString owner = mainActor->decryptSymmetric(QByteArray::fromStdString(tmp["owner"]));
            QString chatId = mainActor->decryptSymmetric(QByteArray::fromStdString(tmp["chatId"]));
            QString pathToUsersFile =
                DfsStruct::ROOT_FOOLDER_NAME + "/" + owner + "/chats/" + chatId + "/users";
            QString pathToMsgFile =
                DfsStruct::ROOT_FOOLDER_NAME + "/" + owner + "/chats/" + chatId + "/0/msg";
            QString pathToUsersFileStored = DfsStruct::ROOT_FOOLDER_NAME + "/" + owner + "/chats/" + chatId
                + "/users" + DfsStruct::STORED_EXT;
            QString pathToMsgFileStored = DfsStruct::ROOT_FOOLDER_NAME + "/" + owner + "/chats/" + chatId
                + "/0/msg" + DfsStruct::STORED_EXT;

            emit requestFile(pathToUsersFile);
            emit requestFile(pathToMsgFile);
            emit requestFile(pathToUsersFileStored);
            emit requestFile(pathToMsgFileStored);
        }
    });
}

Chat *ChatManager::getChatMemory(QByteArray chatId)
{
    for (auto &chat : _chatList)
    {
        if (chat->getChatId() == chatId)
        {
            return chat;
        }
    }

    return nullptr;
}

ChatManager::~ChatManager()
{
    _chatList.clear();
    // delete _accController;
}

void ChatManager::ActorInit()
{
    this->_currentActorId = this->_accController->getMainActor()->id().toActorId();
    /*
    QFile file("keystore/personal/currentID");
    file.open(QIODevice::ReadWrite);
    file.resize(0);
    file.write(this->_currentActorId);
    file.close();
    */
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
