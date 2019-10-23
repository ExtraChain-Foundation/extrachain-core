#include "chat.h"

void Chat::saveChatKey(QByteArray key, QByteArray sessionNumb)
{
    QFile file(keyStore + this->_chatId + "/key" + sessionNumb);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(key);
        file.close();
        return;
    }
    qDebug() << "[Error] Chat manager can't open file to save the key";
}
Chat::Chat(QByteArray chatId, AccountController* accountController)
{
    this->_chatId = chatId;
    this->_encryptionKey = unloadChatKey();
    this->_accountController = accountController;
    this->_currentSession = getCurrentSession();
}

Chat::Chat(QByteArray chatId, QByteArray key, QByteArray currentSession, AccountController* accountController)
{
    QDir().mkpath(chatStore + chatId + "/");
    QDir().mkpath(keyStore + chatId + "/");
    QFile file(chatStore + chatId + "/0");
    file.open(QIODevice::WriteOnly);
    file.close();
    this->_chatId = chatId;
    this->_encryptionKey = key;
    this->_accountController = accountController;
    this->_currentSession = currentSession;
    createNewSession(key, currentSession);
}

Chat::Chat(const Chat& tempChat)
{
    this->_chatId = tempChat.getChatId();
    this->_encryptionKey = tempChat.getEncryptionKey();
    this->_currentSession = tempChat.getSession();
    this->_accountController = tempChat.getAccountController();
}
QByteArray Chat::unloadChatKey()
{
    QFile file(keyStore + this->_chatId + "/key" + getCurrentSession());
    if (!file.exists())
        return "0";
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray key = file.readLine();
        file.close();
        return key;
    }
    qDebug() << "[Error] Chat manager can't open file to load the key";
    return "0";
}

QByteArray Chat::getCurrentSession()
{
    if (this->_currentSession != "-1")
        return this->_currentSession;
    QFile file(chatStore + this->_chatId + "/" + "currentSession");
    if (!file.exists())
    {
        QByteArray currentSession = "0";
        if (file.open(QIODevice::WriteOnly))
        {
            currentSession = findCurrentSession();
            file.write(currentSession);
            file.close();
            this->_currentSession = currentSession;
            return currentSession;
        }
        else
            qDebug() << "[Warning] cannot open file to write session in chat manager";
        return "-1";
    }

    if (file.open(QIODevice::ReadOnly))
    {
        this->_currentSession = file.readLine();
        file.close();
        return this->_currentSession;
    }
    qDebug() << "[Error] Chat manager can't open file to load the key";
    return "-1";
}
QByteArray Chat::findCurrentSession()
{
    BigNumber currentSession("-1");
    QFile file;
    do
    {
        currentSession++;
        file.setFileName(chatStore + this->_chatId + "/" + currentSession.toByteArray());
    } while (file.exists());
    currentSession--;
    return currentSession.toByteArray();
}
QByteArray Chat::createNewSession(QByteArray key, QByteArray sessionNumb)
{
    if (!SaveSession((chatStore + this->_chatId + "/" + "currentSession"), sessionNumb))
        return "-1";
    if (!SaveSession((keyStore + this->_chatId + "/" + "currentSession"), sessionNumb))
        return "-1";
    saveChatKey(key, sessionNumb);
    return sessionNumb;
}
QByteArray Chat::getChatPrivateKey()
{
    return _accountController->getCurrentActor().getKey()->decrypt(unloadChatKey());
}
QByteArray Chat::encryptMessage(QByteArray message)
{
    return blowFish_crypt().EncryptBlowFish(message, getChatPrivateKey());
}
QByteArray Chat::decryptMessage(QByteArray message)
{
    return encryptMessage(message);
}

bool Chat::SaveSession(QByteArray sessionPath, QByteArray sessionNumb)
{
    QFile file(sessionPath);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(sessionNumb);
        file.close();
        return true;
    }

    qDebug() << "[Warning] Error when write sessionpath" << sessionPath << " sessionnumb " << sessionNumb;
    return false;
}

void Chat::sendMessage(QByteArray message)
{
    emit sendMessageToChat(this->_chatId, getCurrentSession(), _accountController->getCurrentActor().getId(),
                           encryptMessage(message));
}

QByteArray Chat::receiveMessage(QByteArray message)
{
    QByteArray receiveMessage = decryptMessage(message);
    // emit signall receive message
    return receiveMessage;
}

QByteArray Chat::getChatId() const
{
    return this->_chatId;
}

QByteArray Chat::getEncryptionKey() const
{
    return this->_encryptionKey;
}

QByteArray Chat::getSession() const
{
    return this->_currentSession;
}

AccountController* Chat::getAccountController() const
{
    return this->_accountController;
}

Chat::~Chat()
{
    delete _accountController;
}
