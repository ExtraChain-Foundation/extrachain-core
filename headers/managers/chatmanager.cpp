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

bool ChatManager::isValid(BigNumber chatId)
{
    return !QDir(chatStore + chatId.toByteArray()).exists();
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

void ChatManager::removeMemberFromChat(QByteArray chatId, BigNumber actorId)
{
    Chat tempChat(chatId, _accController);
    if (tempChat.createNewSession(
            KeyPublic(_accController->getCurrentActor().getKey()->getPublicKey()).encrypt(generateChatKey()),
            (BigNumber(tempChat.getCurrentSession()) + BigNumber("1")).toByteArray())
        != "-1")
    {
        // send key to all member without actorId in blockchain
        //    for(int i=0;i<quantityChatMember;i++)
        //    {
        // if(i!=actorId)
        //        sendInviteToChat(chatid,session,i,key)
        //    }
    }
    else
        qDebug() << "Error when remove Member from chat";
}

void ChatManager::addMemberToChat(QByteArray chatId, BigNumber actorId)
{
    Chat tempChat(chatId, _accController);
    QByteArray key = tempChat.unloadChatKey();
    if (key != "0")
    {
        key = KeyPublic(_accController->getActor(actorId).getKey()->getPublicKey())
                  .encrypt(tempChat.getChatPrivateKey());
        emit sendInviteToChat(chatId, tempChat.getCurrentSession(), actorId, key);
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
    _chatList.push_front(Chat(chatId, key, QByteArray("0"), _accController));
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
