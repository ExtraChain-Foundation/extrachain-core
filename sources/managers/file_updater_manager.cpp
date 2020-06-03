#include "managers/file_updater_manager.h"

FileUpdaterManager::FileUpdaterManager(QObject *parent)
    : QObject(parent)
{
}

FileUpdaterManager::~FileUpdaterManager()
{
}

void FileUpdaterManager::checkAllFiles()
{
    QDir dir(DfsStruct::ROOT_FOOLDER_NAME);
    QStringList listUsers = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &userId : listUsers)
    {
        checkUserFiles(userId.toUtf8());
    }
}

void FileUpdaterManager::checkUserFiles(const QByteArray &userId)
{
    // Chats
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/chats/";
        QDir folderUser(folder);
        QStringList listDataUser = folderUser.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto tmpUserFolder : listDataUser)
        {
            QString nameFile(folder + tmpUserFolder + "/users");
            sendEditDB(nameFile.toUtf8(), "Users", userId, "users", DfsStruct::Type::Chat, chatUser);
            nameFile = folder + tmpUserFolder + "/0/msg";
            sendEditDB(nameFile.toUtf8(), "Chat", userId, "msg", DfsStruct::Type::Chat, chatMessage);
        }
    }
    // Events
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/events/";
        QDir folderUser(folder);
        QStringList listDataUser = folderUser.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto tmpUserFolder : listDataUser)
        {
            QDir folderEvent(folder);
            QStringList listFileEvent = folderEvent.entryList(QDir::Files);
            for (const auto &tmp : listFileEvent)
            {
                if (!tmp.contains("."))
                {
                    QString nameFile(folder + tmpUserFolder + "/" + tmp);
                    sendEditDB(nameFile.toUtf8(), "Properties", userId, tmp, DfsStruct::Type::Event,
                               eventProperties);
                    sendEditDB(nameFile.toUtf8(), "Attachments", userId, tmp, DfsStruct::Type::Event,
                               attachPost);
                    sendEditDB(nameFile.toUtf8(), "Text", userId, tmp, DfsStruct::Type::Event, textPost);
                }
                else if (!tmp.contains("stored"))
                {
                    if (tmp.contains(".comments"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        sendEditDB(nameFile.toUtf8(), "Comments", userId, tmp + ".comments",
                                   DfsStruct::Type::Event, commentsPost);
                        sendEditDB(nameFile.toUtf8(), "Likes", userId, tmp + ".comments",
                                   DfsStruct::Type::Event, commentsLikesPost);
                    }
                    else if (tmp.contains(".likes"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        sendEditDB(nameFile.toUtf8(), "Likes", userId, tmp + ".likes", DfsStruct::Type::Event,
                                   likesPost);
                    }
                    else if (tmp.contains(".users"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        sendEditDB(nameFile.toUtf8(), "Users", userId, tmp + ".users", DfsStruct::Type::Event,
                                   eventUser);
                    }
                }
            }
        }
    }
    // Posts
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/posts/";
        QDir folderUser(folder);
        QStringList listDataUser = folderUser.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto tmpUserFolder : listDataUser)
        {
            QDir folderEvent(folder);
            QStringList listFileEvent = folderEvent.entryList(QDir::Files);
            for (const auto &tmp : listFileEvent)
            {
                if (!tmp.contains("."))
                {
                    QString nameFile(folder + tmpUserFolder + "/" + tmp);
                    sendEditDB(nameFile.toUtf8(), "Properties", userId, tmp, DfsStruct::Type::Post,
                               eventProperties);
                    sendEditDB(nameFile.toUtf8(), "Attachments", userId, tmp, DfsStruct::Type::Post,
                               attachPost);
                    sendEditDB(nameFile.toUtf8(), "Text", userId, tmp, DfsStruct::Type::Post, textPost);
                    sendEditDB(nameFile.toUtf8(), "UsersMarked", userId, tmp, DfsStruct::Type::Post,
                               usersMarked);
                }
                else if (!tmp.contains("stored"))
                {
                    if (tmp.contains(".comments"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        sendEditDB(nameFile.toUtf8(), "Comments", userId, tmp + ".comments",
                                   DfsStruct::Type::Post, commentsPost);
                        sendEditDB(nameFile.toUtf8(), "Likes", userId, tmp + ".comments",
                                   DfsStruct::Type::Post, commentsLikesPost);
                    }
                    else if (tmp.contains(".likes"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        sendEditDB(nameFile.toUtf8(), "Likes", userId, tmp + ".likes", DfsStruct::Type::Post,
                                   likesPost);
                    }
                }
            }
        }
    }
    // Private
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/private/";
        QDir folderUser(folder);

        QString nameFile(folder + "chats");
        sendEditDB(nameFile.toUtf8(), "ChatId", userId, "chats", DfsStruct::Type::Private, chatId);
        nameFile = folder + "events";
        sendEditDB(nameFile.toUtf8(), "Events", userId, "events", DfsStruct::Type::Private, savedEvents);
        nameFile = folder + "likes";
        sendEditDB(nameFile.toUtf8(), "Events", userId, "likes", DfsStruct::Type::Private, likedEvents);
        sendEditDB(nameFile.toUtf8(), "Posts", userId, "likes", DfsStruct::Type::Private, savedPosts);
        nameFile = folder + "notifications";
        sendEditDB(nameFile.toUtf8(), "Notification", userId, "notifications", DfsStruct::Type::Private,
                   notification);
        nameFile = folder + "saved";
        sendEditDB(nameFile.toUtf8(), "Events", userId, "saved", DfsStruct::Type::Private, savedEvents);
        sendEditDB(nameFile.toUtf8(), "Posts", userId, "saved", DfsStruct::Type::Private, savedPosts);
    }
    // Root
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/root";
        sendEditDB(folder.toUtf8(), "Items", userId, "root", DfsStruct::Type::Private,
                   cardFile); // TODO : DfsStruct::Type Root ?
        sendEditDB(folder.toUtf8(), "ItemsDeleted", userId, "root", DfsStruct::Type::Private, cardFile);
    }
}

void FileUpdaterManager::verifyMyFiles(const QByteArray &userId)
{
    // Chats
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/chats/";
        QDir folderUser(folder);
        QStringList listDataUser = folderUser.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto tmpUserFolder : listDataUser)
        {
            QString nameFile(folder + tmpUserFolder + "/users");
            checkVersionFile(nameFile.toUtf8(), "Users", chatUser);
            nameFile = folder + tmpUserFolder + "/0/msg";
            checkVersionFile(nameFile.toUtf8(), "Chat", chatMessage);
        }
    }
    // Events
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/events/";
        QDir folderUser(folder);
        QStringList listDataUser = folderUser.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto tmpUserFolder : listDataUser)
        {
            QDir folderEvent(folder);
            QStringList listFileEvent = folderEvent.entryList(QDir::Files);
            for (const auto &tmp : listFileEvent)
            {
                if (!tmp.contains("."))
                {
                    QString nameFile(folder + tmpUserFolder + "/" + tmp);
                    checkVersionFile(nameFile.toUtf8(), "Properties", eventProperties);
                    checkVersionFile(nameFile.toUtf8(), "Attachments", attachPost);
                    checkVersionFile(nameFile.toUtf8(), "Text", textPost);
                }
                else if (!tmp.contains("stored"))
                {
                    if (tmp.contains(".comments"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        checkVersionFile(nameFile.toUtf8(), "Comments", commentsPost);
                        checkVersionFile(nameFile.toUtf8(), "Likes", commentsLikesPost);
                    }
                    else if (tmp.contains(".likes"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        checkVersionFile(nameFile.toUtf8(), "Likes", likesPost);
                    }
                    else if (tmp.contains(".users"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        checkVersionFile(nameFile.toUtf8(), "Users", eventUser);
                    }
                }
            }
        }
    }
    // Posts
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/posts/";
        QDir folderUser(folder);
        QStringList listDataUser = folderUser.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto tmpUserFolder : listDataUser)
        {
            QDir folderEvent(folder);
            QStringList listFileEvent = folderEvent.entryList(QDir::Files);
            for (const auto &tmp : listFileEvent)
            {
                if (!tmp.contains("."))
                {
                    QString nameFile(folder + tmpUserFolder + "/" + tmp);
                    checkVersionFile(nameFile.toUtf8(), "Properties", eventProperties);
                    checkVersionFile(nameFile.toUtf8(), "Attachments", attachPost);
                    checkVersionFile(nameFile.toUtf8(), "Text", textPost);
                    checkVersionFile(nameFile.toUtf8(), "UsersMarked", usersMarked);
                }
                else if (!tmp.contains("stored"))
                {
                    if (tmp.contains(".comments"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        checkVersionFile(nameFile.toUtf8(), "Comments", commentsPost);
                        checkVersionFile(nameFile.toUtf8(), "Likes", commentsLikesPost);
                    }
                    else if (tmp.contains(".likes"))
                    {
                        QString nameFile(folder + tmpUserFolder + "/" + tmp);
                        checkVersionFile(nameFile.toUtf8(), "Likes", likesPost);
                    }
                }
            }
        }
    }
    // Private
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/private/";
        QDir folderUser(folder);

        QString nameFile(folder + "chats");
        checkVersionFile(nameFile.toUtf8(), "ChatId", chatId);
        nameFile = folder + "events";
        checkVersionFile(nameFile.toUtf8(), "Events", savedEvents);
        nameFile = folder + "likes";
        checkVersionFile(nameFile.toUtf8(), "Events", likedEvents);
        checkVersionFile(nameFile.toUtf8(), "Posts", savedPosts);
        nameFile = folder + "notifications";
        checkVersionFile(nameFile.toUtf8(), "Notification", notification);
        nameFile = folder + "saved";
        checkVersionFile(nameFile.toUtf8(), "Events", savedEvents);
        checkVersionFile(nameFile.toUtf8(), "Posts", savedPosts);
    }
    // Root
    {
        QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/root";
        checkVersionFile(folder.toUtf8(), "Items", cardFile);
        checkVersionFile(folder.toUtf8(), "ItemsDeleted", cardFile);
    }
}

void FileUpdaterManager::sendEditDB(const QByteArray &filePath, const QByteArray &nameTable,
                                    const QString &userId, const QString &nameFile,
                                    const DfsStruct::Type &type, const QByteArrayList &listProve)
{
    // TODO: maybe add type new column
    if (QFile().exists(filePath))
    {
        DBConnector db;
        if (db.open(filePath.toStdString()))
        {
            std::vector<DBRow> res = db.select("PRAGMA table_info('" + nameTable.toStdString() + "')");
            if (res.size() != 0)
            {
                QByteArrayList listExist;
                for (auto &tmp : res)
                    listExist.append(tmp["name"].c_str());
                for (const auto &tmp : listExist)
                {
                    if (!listProve.contains(tmp))
                    {
                        emit editDB(userId, nameFile, type, DfsStruct::ChangeType::RemoveColumn,
                                    { nameTable, tmp });
                    }
                }
                for (const auto &tmp : listProve)
                {
                    if (!listExist.contains(tmp))
                    {
                        emit editDB(userId, nameFile, type, DfsStruct::ChangeType::NewColumn,
                                    { nameTable, tmp });
                    }
                }
            }
        }
        db.close();
    }
}

void FileUpdaterManager::checkVersionFile(const QByteArray &filePath, const QByteArray &nameTable,
                                          const QByteArrayList &listVerify)
{
    if (QFile().exists(filePath))
    {
        DBConnector db;
        if (db.open(filePath.toStdString()))
        {
            std::vector<DBRow> res = db.select("PRAGMA table_info('" + nameTable.toStdString() + "')");
            if (res.size() != 0)
            {
                QByteArrayList listExist;
                for (auto &tmp : res)
                    listExist.append(tmp["name"].c_str());
                if (listExist != listVerify)
                    emit sendGetNewVersFile(filePath);
            }
        }
        db.close();
    }
}

void FileUpdaterManager::checkRoot(const QString &userId, const QString &ver)
{

    QString folder = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/root";
    DBConnector db(folder.toStdString());
    if (db.open("Items"))
    {
        auto res = db.select("SELECT * FROM Items WHERE version = '" + ver.toStdString() + "'");
        QByteArrayList list;
        for (auto &tmp : res)
        {
            list.append(tmp["type"].c_str());
            list.append(tmp["id"].c_str());
        }
        // editSomeFiles(userId, list);
    }
    db.close();
}
