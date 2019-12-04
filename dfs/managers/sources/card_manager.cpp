#include "dfs/managers/headers/card_manager.h"
#include <QMutex>
#include <QUrl>

QStringList CardManager::getFilesByType(const QString &userId, dfsStruct::Type type)
{
    QString path(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/');
    DBConnector dbConnect;
    QStringList listData;
    if (!dbConnect.open(path.toStdString() + dfsStruct::ACTOR_CARD_FILE.toStdString()))
    {
        qDebug() << "[Error][Card_Manager][getFilesByType] seva ne lomay bazy dannnuzx";
        return QStringList();
    }
    QByteArray query = "SELECT path FROM " + QByteArray(Config::DataStorage::cardTableName.c_str())
        + " WHERE type=" + QByteArray::number(type) + ';';
    std::vector<DBRow> data = dbConnect.select(query.toStdString());
    for (DBRow &temp : data)
        listData.append(temp["path"].c_str());
    return listData;
}

QByteArray CardManager::getLastFileName(const QString &userId, dfsStruct::Type type)
{
    QByteArray path(dfsStruct::ROOT_FOOLDER_NAME.toLocal8Bit() + '/' + userId.toLocal8Bit() + '/');
    DBConnector dbConnect;
    if (!dbConnect.open(path.toStdString() + dfsStruct::ACTOR_CARD_FILE.toStdString()))
    {
        qDebug() << "[Error][Card_Manager][getLastFileName] seva ne lomay bazy dannnuzx";
        return QByteArray();
    }
    QByteArray t = QByteArray::number(type);
    std::vector<DBRow> res =
        dbConnect.select(("SELECT counter FROM " + QByteArray(Config::DataStorage::lsTableName.c_str())
                          + " WHERE type='" + t + "';")
                             .toStdString());
    if (res.empty())
        return "-1";
    QByteArray bres = QByteArray::fromStdString(res[0]["counter"]);
    return bres.isEmpty() ? "-1" : bres;
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
        qDebug() << "[Error][Card_Manager][getAllFiles] seva ne lomay bazy dannnuzx";
        return QStringList();
    }
    QByteArray query = "SELECT path FROM ITEMS";

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

QStringList CardManager::getAll(dfsStruct::Type type)
{
    QStringList all;

    const QStringList allUserIds =
        QDir(dfsStruct::ROOT_FOOLDER_NAME).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (auto &userId : allUserIds)
    {
        QStringList files = getFilesByType(userId.toLatin1(), type);
        all << files;
    }

    return all;
}

QString CardManager::buildPathForFile(const QString &userId, const QString &file, dfsStruct::Type type,
                                      bool localFormat)
{
    const QString currentPath = QUrl::fromLocalFile(QDir::currentPath()).toString() + "/";
    return (localFormat ? currentPath : "") + dfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/"
        + dfsStruct::toString(type) + "/"
        + (BigNumber(file.toLatin1()) / BigNumber(Config::DataStorage::SECTION_SIZE)).toByteArray() + "/"
        + file;
}

QStringList CardManager::buildPathForFiles(const QString &userId, const QStringList &files,
                                           dfsStruct::Type type, bool localFormat)
{
    QStringList result;

    for (const QString &file : files)
    {
        result << buildPathForFile(userId, file, type, localFormat);
    }

    return result;
}

CardManager::CardManager()
{
}
