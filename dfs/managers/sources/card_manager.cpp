#include "dfs/managers/headers/card_manager.h"
#include <QMutex>
#include <QUrl>

std::vector<std::string> CardManager::getFilesByType(const std::string &userId, dfsStruct::Type type)
{
    DBConnector dbConnect;

    if (!dbConnect.open(pathToRoot(userId)))
    {
        qDebug() << "[Error][Card_Manager][getFilesByType]";
        return {};
    }

    std::string query = "SELECT path FROM " + Config::DataStorage::cardTableName
        + " WHERE type=" + std::to_string(type) + ';';
    std::vector<DBRow> data = dbConnect.select(query);

    std::vector<std::string> listData;

    for (DBRow &temp : data)
        listData.push_back(temp["path"]);

    return listData;
}

std::string CardManager::getLastFileName(const std::string &userId, dfsStruct::Type type)
{
    DBConnector dbConnect;

    if (!dbConnect.open(pathToRoot(userId)))
    {
        qDebug() << "[Error][Card_Manager][getLastFileName]";
        return "";
    }

    std::string query = "SELECT counter FROM " + Config::DataStorage::lsTableName + " WHERE type='"
        + std::to_string(type) + "';";
    std::vector<DBRow> res = dbConnect.select(query);

    if (res.empty())
        return "-1";

    std::string counter = res[0]["counter"];
    return counter.empty() ? "-1" : counter;
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
    QString path(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/');
    DBConnector dbConnect;
    QStringList listData;
    if (!dbConnect.open(path.toStdString() + dfsStruct::ACTOR_CARD_FILE.toStdString()))
    {
        qDebug() << "[Error][Card_Manager][getAllFiles]";
        return QStringList();
    }
    QByteArray query = "SELECT path FROM " + QByteArray(Config::DataStorage::cardTableName.c_str());

    std::vector<DBRow> data = dbConnect.select(query.toStdString());
    for (DBRow &temp : data)
        listData.append(QString::fromStdString(temp["path"]).toLocal8Bit());

    return listData;
}

dfsStruct::Type CardManager::getTypeByName(const QString &path, const QByteArray &userId)
{
    QString pathLocal(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/');
    if (path == pathLocal + dfsStruct::ACTOR_CARD_FILE)
    {
        return dfsStruct::Type::card;
    }
    DBConnector dbConnect;
    QStringList listData;
    static QMutex mutex;
    mutex.lock();
    if (!dbConnect.open(pathLocal.toStdString() + dfsStruct::ACTOR_CARD_FILE.toStdString()))
    {
        //        qDebug() << "[Error][Card_Manager][getTypeByName] dimka nividimka";
        return dfsStruct::service;
    }
    QByteArray query = "SELECT type FROM " + QByteArray(Config::DataStorage::cardTableName.c_str())
        + " WHERE path=" + "'" + path.toUtf8() + "'" + ';';

    std::vector<DBRow> data = dbConnect.select(query.toStdString());
    mutex.unlock();
    if (data.empty())
    {
        return dfsStruct::Type::unknown;
    }
    QString x = QString::fromStdString(data[0]["type"]);

    return dfsStruct::convertToDFType(x.toLocal8Bit());
}

std::string CardManager::pathToRoot(std::string userId)
{
    return dfsStruct::ROOT_FOOLDER_NAME.toStdString() + '/' + userId + '/'
        + dfsStruct::ACTOR_CARD_FILE.toStdString();
}

std::vector<std::string> CardManager::getAll(dfsStruct::Type type)
{
    std::vector<std::string> all;

    const QStringList allUserIds =
        QDir(dfsStruct::ROOT_FOOLDER_NAME).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (auto &userId : allUserIds)
    {
        std::vector<std::string> files = getFilesByType(userId.toStdString(), type);
        for (const std::string &file : files)
            all.push_back(file);
    }

    return all;
}

std::string CardManager::buildPathForFile(const std::string &userId, const std::string &file,
                                          dfsStruct::Type type, bool localFormat)
{
    if (file.empty())
        return "";

    const std::string currentPath =
        (localFormat ? QUrl::fromLocalFile(QDir::currentPath()).toString().toStdString() + "/" : "")
        + dfsStruct::ROOT_FOOLDER_NAME.toStdString() + "/" + userId;
    const std::string section =
        (BigNumber(file.c_str()) / BigNumber(Config::DataStorage::SECTION_SIZE)).toStdString();
    std::string typeName = dfsStruct::toString(type).toStdString();
    std::string path = currentPath + "/" + typeName + "/" + section + "/" + file;

    return path;
}

std::vector<std::string> CardManager::buildPathForFiles(const std::string &userId,
                                                        const std::vector<std::string> &files,
                                                        dfsStruct::Type type, bool localFormat)
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
