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
    QDir dir("data");
    QStringList listUsers = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto userId : listUsers)
    {
        // Chats
        {
            QString folder = "data/" + userId + "/chats/";
            QDir folderUser(folder);
            QStringList listDataUser = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
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
            QString folder = "data/" + userId + "/events/";
            QDir folderUser(folder);
            QStringList listDataUser = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const auto tmpUserFolder : listDataUser)
            {
                QDir folderEvent(folder);
                QStringList listFileEvent = dir.entryList(QDir::Files);
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
                            nameFile = folder + tmpUserFolder + "/" + tmp;
                            sendEditDB(nameFile.toUtf8(), "Likes", userId, tmp + ".comments",
                                       DfsStruct::Type::Event, commentsLikesPost);
                        }
                        else if (tmp.contains(".likes"))
                        {
                            QString nameFile(folder + tmpUserFolder + "/" + tmp);
                            sendEditDB(nameFile.toUtf8(), "Likes", userId, tmp + ".likes",
                                       DfsStruct::Type::Event, likesPost);
                        }
                        else if (tmp.contains(".users"))
                        {
                            QString nameFile(folder + tmpUserFolder + "/" + tmp);
                            sendEditDB(nameFile.toUtf8(), "Users", userId, tmp + ".users",
                                       DfsStruct::Type::Event, eventUser);
                        }
                    }
                }
            }
        }
        // Posts
        {
            QString folder = "data/" + userId + "/posts/";
            QDir folderUser(folder);
            QStringList listDataUser = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const auto tmpUserFolder : listDataUser)
            {
                QDir folderEvent(folder);
                QStringList listFileEvent = dir.entryList(QDir::Files);
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
                            nameFile = folder + tmpUserFolder + "/" + tmp;
                            sendEditDB(nameFile.toUtf8(), "Likes", userId, tmp + ".comments",
                                       DfsStruct::Type::Post, commentsLikesPost);
                        }
                        else if (tmp.contains(".likes"))
                        {
                            QString nameFile(folder + tmpUserFolder + "/" + tmp);
                            sendEditDB(nameFile.toUtf8(), "Likes", userId, tmp + ".likes",
                                       DfsStruct::Type::Post, likesPost);
                        }
                    }
                }
            }
        }
        // Private
        {
            QString folder = "data/" + userId + "/private/";
            QDir folderUser(folder);
            QStringList listDataUser = dir.entryList(QDir::Files);

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
            QString folder = "data/" + userId + "/root";
            sendEditDB(folder.toUtf8(), "Items", userId, "root", DfsStruct::Type::Private,
                       cardFile); // TODO : DfsStruct::Type Root ?
            sendEditDB(folder.toUtf8(), "ItemsDeleted", userId, "root", DfsStruct::Type::Private, cardFile);
        }
        // Add? -> company/service/usernames
        // stored?
    }
}

void FileUpdaterManager::sendEditDB(const QByteArray &filePath, const QByteArray &nameTable,
                                    const QString &userId, const QString &nameFile,
                                    const DfsStruct::Type &type, const QByteArrayList &listProve)
{
    if (QFile().exists(filePath))
    {
        DBConnector db(filePath.toStdString());
        if (db.open(nameTable.toStdString()))
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
                        emit editDB(
                            userId, nameFile, type,
                            DfsStruct::ChangeType::Delete, // TODO: add DfsStruct::ChangeType::deleteRow + add
                                                           // in editSql()
                            { nameTable, tmp });
                    }
                }
                for (const auto &tmp : listProve)
                {
                    if (!listExist.contains(tmp))
                    {
                        emit editDB(userId, nameFile, type,
                                    DfsStruct::ChangeType::Insert, // TODO: DfsStruct::ChangeType::AddRow(with
                                                                   // type?) + add in editSql()
                                    { nameTable, tmp });
                    }
                }
            }
        }
        db.close();
    }
}

void FileUpdaterManager::checkUserFiles(const QByteArray &userId)
{
}
