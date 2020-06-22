#include "dfs/managers/headers/card_manager.h"
#include <QMutex>
#include <QUrl>

std::vector<std::string> CardManager::getFilesByType(const std::string &userId, DfsStruct::Type type)
{ // ignore "."?
    DBConnector dbConnect;

    if (!QFile::exists(pathToRoot(userId).c_str()))
        return {};
    if (!dbConnect.open(pathToRoot(userId)))
    {
        qDebug() << "[Error][Card_Manager][getFilesByType]";
        return {};
    }

    std::string query =
        "SELECT id FROM " + Config::DataStorage::cardTableName + " WHERE type=" + std::to_string(type) + ';';
    std::vector<DBRow> data = dbConnect.select(query);

    std::vector<std::string> listData;

    for (DBRow &temp : data)
        listData.push_back(CardManager::buildPathForFile(userId, temp["id"], type));

    return listData;
}

QStringList CardManager::getAllFiles(const QByteArray &userId)
{
    //    QFile card(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + dfsStruct::ACTOR_CARD_FILE);
    //    if (!card.exists())
    //        return {};
    //    card.open(QIODevice::ReadOnly);
    //    QList<QByteArray> list =
    //        Serialization::deserialize(card.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    //    if (list.isEmpty())
    //        return QStringList();

    //    for (const QByteArray &el : list)

    //        result << Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2);
    QString path(DfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/');
    DBConnector dbConnect;
    QStringList listData;
    if (!dbConnect.open(path.toStdString() + DfsStruct::ACTOR_CARD_FILE.toStdString()))
    {
        qDebug() << "[Error][Card_Manager][getAllFiles]";
        return QStringList();
    }
    QByteArray query = "SELECT id, type FROM " + QByteArray(Config::DataStorage::cardTableName.c_str());

    std::vector<DBRow> data = dbConnect.select(query.toStdString());
    for (DBRow &temp : data)
    {
        std::string path = CardManager::buildPathForFile(userId.toStdString(), temp["id"],
                                                         DfsStruct::Type(std::stoi(temp["type"])));
        listData.append(QByteArray::fromStdString(path));
    }

    return listData;
}

DfsStruct::Type CardManager::getTypeByName(const QString &fullPath)
{
    QString userId = fullPath.mid(DfsStruct::ROOT_FOOLDER_NAME_MID, 20);
    bool hasSection = false;
    // int fromType = fullPath.indexOf("/", 26);
    int from = fullPath.indexOf("/", 20 + DfsStruct::ROOT_FOOLDER_NAME_MID + 2) + 1;
    // int fromSection = fullPath.indexOf("/", from) + 1;
    hasSection = fullPath[from + 2] == "/";
    // qDebug() << fullPath << fullPath[from + 2] << hasSection << fullPath.mid(hasSection ? fromSection :
    // from);
    QString type = fullPath.mid(20 + DfsStruct::ROOT_FOOLDER_NAME_MID + 1);
    type = type.left(type.indexOf("/"));
    // qDebug() << type;

    return DfsStruct::toDfsType(type.toLatin1());
}

std::string CardManager::pathToRoot(std::string userId)
{
    return DfsStruct::ROOT_FOOLDER_NAME.toStdString() + '/' + userId + '/'
        + DfsStruct::ACTOR_CARD_FILE.toStdString();
}

std::vector<std::string> CardManager::getAll(DfsStruct::Type type)
{
    std::vector<std::string> all;

    const QStringList allUserIds =
        QDir(DfsStruct::ROOT_FOOLDER_NAME).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (auto &userId : allUserIds)
    {
        std::vector<std::string> files = getFilesByType(userId.toStdString(), type);
        for (const std::string &file : files)
            all.push_back(file);
    }

    return all;
}

std::string CardManager::buildPathForFile(const std::string &userId, const std::string &file,
                                          DfsStruct::Type type, PathStyle pathFormat)
{
    if (file.empty())
        return "";

    const std::string currentPath =
        (pathFormat == PathStyle::FullLocal
             ? QUrl::fromLocalFile(QDir::currentPath()).toString().toStdString() + "/"
             : "")
        + DfsStruct::ROOT_FOOLDER_NAME.toStdString() + "/" + userId;

    QString fileStr = QString::fromStdString(file);
    std::string section = fileStr.contains(".")
        ? fileStr.mid(fileStr.indexOf(".") - 2, 2).toStdString() + "/"
        : QByteArray::fromStdString(file).right(2).toStdString() + "/";

    if (int(type) > 100)
    {
        type = DfsStruct::Type(static_cast<int>(type) - 100);
        section = "";
    }

    std::string typeName = DfsStruct::toString(type).toStdString();
    std::string path = currentPath + "/" + typeName + "/" + section + file;

    return path;
}

std::vector<std::string> CardManager::buildPathForFiles(const std::string &userId,
                                                        const std::vector<std::string> &files,
                                                        DfsStruct::Type type, PathStyle pathFormat)
{
    std::vector<std::string> result;

    for (const std::string &file : files)
        result.push_back(buildPathForFile(userId, file, type, pathFormat));

    return result;
}

QString CardManager::cutPath(QString fullPath)
{
    QString userId = fullPath.mid(DfsStruct::ROOT_FOOLDER_NAME_MID, 20);
    bool hasSection = false;
    // int fromType = fullPath.indexOf("/", 26);
    int from = fullPath.indexOf("/", 27) + 1;
    int fromSection = fullPath.indexOf("/", from) + 1;
    hasSection = fullPath[from + 2] == "/";
    // qDebug() << fullPath << fullPath[from + 2] << hasSection << fullPath.mid(hasSection ? fromSection :
    // from);
    QString type = fullPath.mid(20 + DfsStruct::ROOT_FOOLDER_NAME_MID + 1);
    type = type.left(type.indexOf("/"));
    // qDebug() << type;

    if (fullPath.contains(".comments") || fullPath.contains(".likes"))
    {
        hasSection = false;
    }

    return fullPath.mid(hasSection ? fromSection : from);
}

int CardManager::dfsVersion(QString path)
{
    using namespace DfsStruct;
    QString name = cutPath(path);
    DfsStruct::Type type = getTypeByName(path);

    switch (type)
    {
    case DfsStruct::Type::Post:
        if (name.contains(".comments"))
            return dfsVersions[DfsVersionType::PostCommentsVersion];
        else if (name.contains(".likes"))
            return dfsVersions[DfsVersionType::PostLikesVersion];
        else
            return dfsVersions[DfsVersionType::PostVersion];
        break;
    case DfsStruct::Type::Event:
        if (name.contains(".comments"))
            return dfsVersions[DfsVersionType::EventCommentsVersion];
        else if (name.contains(".likes"))
            return dfsVersions[DfsVersionType::EventLikesVersion];
        else if (name.contains(".users"))
            return dfsVersions[DfsVersionType::EventUsersVersion];
        else
            return dfsVersions[DfsVersionType::EventVersion];
        break;
    case DfsStruct::Type::Private:
        if (name == "chats")
            return dfsVersions[DfsVersionType::PrivateChats];
        else if (name == "events")
            return dfsVersions[DfsVersionType::PrivateEvents];
        else if (name == "likes")
            return dfsVersions[DfsVersionType::PrivateLikes];
        else if (name == "notifications")
            return dfsVersions[DfsVersionType::PrivateNotifications];
        else if (name == "saved")
            return dfsVersions[DfsVersionType::PrivateSaved];
        break;
    case DfsStruct::Type::Service:
        if (name == "chatinvite")
            return dfsVersions[DfsVersionType::ServiceChatInvite];
        else if (name == "events")
            return dfsVersions[DfsVersionType::ServiceEvents];
        else if (name == "follower")
            return dfsVersions[DfsVersionType::ServiceFollower];
        else if (name == "subscribe")
            return dfsVersions[DfsVersionType::ServiceSubscribe];
        break;
    case DfsStruct::Type::Chat:
        if (name.contains("/msg"))
            return dfsVersions[DfsVersionType::ChatMsg];
        else if (name.contains("/users"))
            return dfsVersions[DfsVersionType::ChatUsers];
        break;
    default:
        return 0;
    }

    return 0;
}

CardManager::CardManager()
{
}
