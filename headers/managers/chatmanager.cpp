#include "chatmanager.h"

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
    // send to chat it path
    QDir().mkpath(getPathToMyChats());
    QDirIterator it(getPathToMyChats(), QDirIterator::Subdirectories);
    QByteArray chatPath = "-1";

    while (it.hasNext())
    {
        chatPath = convertChatIdToFullPath(it.fileName().toLocal8Bit());
        if (chatPath == "-1")
        {
            it.next();
            continue;
        }

        Chat *temp = new Chat(it.fileName().toLocal8Bit(), _accController, chatPath);
        if (temp->getMyCurrentSession() == temp->getActualCurrentSession())
            _chatList.push_front(temp);
        it.next();
    }
}

void ChatManager::InitializeConnectSignalSlot()
{
    foreach (Chat *currentChat, _chatList)
        connect(currentChat, &Chat::sendDataToBlockchain, this, &ChatManager::getSignalFromChats);
}

QByteArray ChatManager::convertChatIdToFullPath(QByteArray chatId)
{
    QFile file(getPathToMyChats() + chatId + "/" + chatId + ".dat");
    if (!file.exists())
        return "-1";
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray path = file.readLine();
        file.close();
        return path;
    }
    else
        qDebug() << "[Warning] Cannot open file on write "
                 << getPathToMyChats() + chatId + "/" + chatId + ".dat";
    return "-1";
}

QByteArray ChatManager::generateChatId()
{
    return generateChatKey();
}

QByteArray ChatManager::getPathToMyChats()
{
    return _actorPath + _currentActorId + "/myChats/";
}

QByteArray ChatManager::generateChatKey()
{
    return Utils::calcKeccak(BigNumber::random(64).toByteArray());
}

ChatManager::ChatManager(AccountController *accController)
{
    this->_accController = accController;
    this->_currentActorId = accController->getCurrentActor().getId().toByteArray();
    this->_actorPath = accController->getActorIndex()->getFolderPath().toLocal8Bit();
    InitializeChatList();
    InitializeConnectSignalSlot();
}

void ChatManager::removeMemberFromChat(QByteArray chatId, QByteArray actorId)
{
    Chat temp(chatId, _accController, convertChatIdToFullPath(chatId));
    if (!temp.isUserVerify(_currentActorId) || !temp.isOwner())
    {
        qDebug() << "[Warning] Can't invite to chat. User verify error, removeMemberFromChat. ChatManager";
        return;
    }
    QByteArray currentSession = temp.getMyCurrentSession();
    QByteArray newSession = (BigNumber(currentSession) + BigNumber("1")).toByteArray();
    if (temp.createNewSession(
            KeyPublic(_accController->getCurrentActor().getKey()->getPublicKey()).encrypt(generateChatKey()),
            newSession)
        != "-1")
    {
        QFile file(convertChatIdToFullPath(chatId) + "sessions/" + newSession);
        file.open(QIODevice::WriteOnly);
        file.close();
        getSignalFromChats(convertChatIdToFullPath(chatId) + "sessions/" + newSession);

        QDir directory(_actorPath + _currentActorId + "/chatStorage/" + chatId + "/users/");
        QStringList filesList = directory.entryList(QStringList(), QDir::Files);
        foreach (QString filename, filesList)
        {
            if (temp.isUserActual(filename.toLocal8Bit(), currentSession))
                InviteToChat(temp.getChatId(), actorId);
        }
    }
    else
        qDebug() << "[Error] when remove Member from chat. RemoveMemberFromChat ChatManager";
}

void ChatManager::CreateNewChat()
{
    QByteArray chatId = generateChatId();
    QByteArray key =
        KeyPublic(_accController->getCurrentActor().getKey()->getPublicKey()).encrypt(generateChatKey());
    _chatList.push_front(new Chat(chatId, key, QByteArray("0"), _accController, _currentActorId));
}

void ChatManager::InviteToChat(QByteArray chatId, QByteArray actorId)
{
    Chat temp(chatId, _accController, convertChatIdToFullPath(chatId));
    if (!temp.isUserVerify(_currentActorId)
        || !temp.isUserActual(_currentActorId, temp.getActualCurrentSession()))
    {
        qDebug() << "[Warning] Can't invite to chat. User verify error, InviteToChat. ChatManager";
        return;
    }
    if (temp.isUserVerify(actorId))
        return;
    temp.InviteNewUser(KeyPublic(_accController->getActor(actorId).getKey()->getPublicKey())
                           .encrypt(temp.getChatPrivateKey()),
                       actorId);
}

ChatManager::~ChatManager()
{
    _chatList.clear();
    delete _accController;
}

// void ChatManager::addedNewUserToChat(QByteArray chatId, QByteArray inviterId, QByteArray inviterSign,
//                                     QByteArray invitedId)
//{
//    Chat tempChat(chatId, _accController);
//    tempChat.InviteNewUser(inviterId, inviterSign, invitedId);
//    if (!tempChat.isUserVerify(invitedId))
//        tempChat.removeUserFromChat(invitedId);
//}

void ChatManager::getSignalFromChats(const QString &path)
{
    emit sendDataToBlockhainFromChatManager(path, based_dfs_struct::Type::chates);
}
