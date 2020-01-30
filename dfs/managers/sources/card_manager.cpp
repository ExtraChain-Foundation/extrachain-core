#include "dfs/managers/headers/card_manager.h"
#include <QMutex>
#include <QUrl>

std::vector<std::string> CardManager::getFilesByType(const std::string &userId, DfsStruct::Type type)
{
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
        listData.push_back(CardManager::buildPathForFile(userId, temp["id"], type, false));

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
                                                         DfsStruct::Type(std::stoi(temp["type"])), false);
        listData.append(QByteArray::fromStdString(path));
    }

    return listData;
}

DfsStruct::Type CardManager::getTypeByName(const QString &path, const QByteArray &userId)
{
    QString pathLocal(DfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/');
    if (!QFile::exists(pathLocal + DfsStruct::ACTOR_CARD_FILE)
        || path == pathLocal + DfsStruct::ACTOR_CARD_FILE)
    {
        return DfsStruct::Type::unknown;
    }
    DBConnector dbConnect;
    QStringList listData;
    static QMutex mutex;
    mutex.lock();

    if (!dbConnect.open(pathLocal.toStdString() + DfsStruct::ACTOR_CARD_FILE.toStdString()))
    {
        return DfsStruct::service;
    }

    QByteArray query = "SELECT  type FROM " + QByteArray(Config::DataStorage::cardTableName.c_str())
        + " WHERE id=" + "'" + path.right(path.length() - path.lastIndexOf("/") - 1).toUtf8() + "'" + ';';

    std::vector<DBRow> data = dbConnect.select(query.toStdString());
    mutex.unlock();
    if (data.empty())
    {
        return DfsStruct::Type::unknown;
    }

    int type = -1;
    try
    {
        type = std::stoi(data[0]["type"]);
        if (type > 100)
            type -= 100;
    } catch (std::exception e)
    {
        type = 100;
    }

    return DfsStruct::Type(type);
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
                                          DfsStruct::Type type, bool localFormat)
{
    if (file.empty())
        return "";

    const std::string currentPath =
        (localFormat ? QUrl::fromLocalFile(QDir::currentPath()).toString().toStdString() + "/" : "")
        + DfsStruct::ROOT_FOOLDER_NAME.toStdString() + "/" + userId;
    std::string section = QByteArray::fromStdString(file).right(2).toStdString() + "/";
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
                                                        DfsStruct::Type type, bool localFormat)
{
    std::vector<std::string> result;

    for (const std::string &file : files)
    {
        result.push_back(buildPathForFile(userId, file, type, localFormat));
    }

    return result;
}

CardManager::CardManager()
{
}
