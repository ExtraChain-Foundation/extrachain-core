#include "chatmanager.h"

void ChatManager::Initialize()
{
    QDirIterator it(keyStore, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        QFile file(it.next() + "/currentSession");
        if (!file.exists())
            continue;
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray currentSession = file.readLine();
            Chat temp = Chat(it.fileName().toLocal8Bit(), _accController);
            if (currentSession == temp.getCurrentSession())
                _chatList.push_front(temp);
        }
    }
}

void ChatManager::InitializeConnectSignalSlot()
{
}

QByteArray ChatManager::generateChatId()
{
    return generateChatKey();
}

bool ChatManager::isValid(QByteArray chatId)
{
    return !QDir(chatStore + chatId).exists();
}

QByteArray ChatManager::generateChatKey()
{
    return Utils::calcKeccak(BigNumber::random(64).toByteArray());
}

ChatManager::ChatManager(AccountController *accController)
{
    this->_accController = accController;
    Initialize();
    InitializeConnectSignalSlot();
}

void ChatManager::removeMemberFromChat(QByteArray chatId, QByteArray actorId)
{
    Chat tempChat(chatId, _accController);
    if (tempChat.createNewSession(
            KeyPublic(_accController->getCurrentActor().getKey()->getPublicKey()).encrypt(generateChatKey()),
            (BigNumber(tempChat.getCurrentSession()) + BigNumber("1")).toByteArray())
        != "-1")
    {
        QDir directory(chatStore + chatId + "/users/");
        QStringList filesList = directory.entryList(QStringList(), QDir::Files);
        foreach (QString filename, filesList)
        {
            if (filename.toLocal8Bit() != actorId
                && filename.toLocal8Bit() != _accController->getCurrentActor().getId().toByteArray())
                emit sendInviteToChat(
                    chatId, tempChat.getCurrentSession(), filename.toLocal8Bit(),
                    QByteArray(
                        KeyPublic(_accController->getActor(filename.toLocal8Bit()).getKey()->getPublicKey())
                            .encrypt(tempChat.getChatPrivateKey())));
        }
    }
    else
        qDebug() << "Error when remove Member from chat";
}

void ChatManager::addMemberToChat(QByteArray chatId, QByteArray actorId)
{
    Chat tempChat(chatId, _accController);
    QByteArray key = tempChat.unloadChatKey();
    if (key != "0")
    {
        QByteArray currentId = _accController->getCurrentActor().getId().toByteArray();
        key = KeyPublic(_accController->getActor(actorId).getKey()->getPublicKey())
                  .encrypt(tempChat.getChatPrivateKey());

        tempChat.InviteNewUser(currentId, _accController->getCurrentActor().getKey()->sign(currentId),
                               actorId);
        // also need to emit signal that share new user in blockhain by path chat/users/actorId
    }
}

void ChatManager::CreateNewChat()
{
    QByteArray chatId = "0";
    do
    {
        chatId = generateChatId();
    } while (!isValid(chatId));

    QByteArray key =
        KeyPublic(_accController->getCurrentActor().getKey()->getPublicKey()).encrypt(generateChatKey());
    _chatList.push_front(Chat(chatId, key, QByteArray("0"), _accController,
                              _accController->getCurrentActor().getId().toByteArray()));
    emit sendCreatedNewChat(chatId);
}

ChatManager::~ChatManager()
{
    _chatList.clear();
    delete _accController;
}

void ChatManager::receiveInviteToChat(QByteArray chatId, QByteArray sessionNumb, QByteArray key)
{
    _chatList.push_front(Chat(chatId, key, sessionNumb, _accController));
}

void ChatManager::addedNewUserToChat(QByteArray chatId, QByteArray inviterId, QByteArray inviterSign,
                                     QByteArray invitedId)
{
    Chat tempChat(chatId, _accController);
    tempChat.InviteNewUser(inviterId, inviterSign, invitedId);
    if (!tempChat.isUserVerify(invitedId))
        tempChat.removeUserFromChat(invitedId);
}
