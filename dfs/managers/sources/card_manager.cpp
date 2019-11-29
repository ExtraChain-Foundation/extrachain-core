#include "dfs/managers/headers/card_manager.h"

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
    QByteArray query = "SELECT path FROM ITEMS WHERE type=" + QByteArray::number(type) + ';';

    std::vector<DBRow> data = dbConnect.select(query.toStdString());
    for (DBRow &temp : data)
        listData.append(path + QString::fromStdString(temp["path"]).toLocal8Bit());

    return listData;
    //    card.open(QIODevice::ReadOnly);
    //    QList<QByteArray> list =
    //        Serialization::deserialize(card.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);

    //    QStringList result;
    //    for (const QByteArray &el : list)
    //    {
    //        QByteArray dType =
    //            Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(3);
    //        if (dfsStruct::toByteArray(type) == dType)
    //            result << dfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + dType + "/"
    //                    + Serialization::deserialize(el,
    //                    Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2);
    //    }
}

QByteArray CardManager::getLastFileName(const QString &userId, dfsStruct::Type type)
{
    //    QFile file(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + dfsStruct::ACTOR_CARD_FILE);
    //    if (!file.exists())
    //        return "";
    //    file.open(QIODevice::ReadOnly);

    //    QList<QByteArray> list =
    //        Serialization::deserialize(file.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    //    if (list.isEmpty())
    //        return "0";
    //    return Serialization::deserialize(list.takeLast(),
    //    Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2);
    QByteArray path(dfsStruct::ROOT_FOOLDER_NAME.toLocal8Bit() + '/' + userId.toLocal8Bit() + '/');
    DBConnector dbConnect;
    if (!dbConnect.open(path.toStdString() + dfsStruct::ACTOR_CARD_FILE.toStdString()))
    {
        qDebug() << "[Error][Card_Manager][getLastFileName] seva ne lomay bazy dannnuzx";
        return QByteArray();
    }
    QString query = QString("SELECT LAST(path) FROM ITEMS WHERE type = %1").arg(int(type)); // ORDER by DESC LIMIT 1

    auto row = dbConnect.select(query.toStdString());
    if (row.empty())
        return "0";
    QString pathFile = QString::fromStdString(row[0]["path"]);
    pathFile = pathFile.remove(0, pathFile.lastIndexOf("/"));
    return pathFile.isEmpty() ? "0" : pathFile.toLatin1();
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
    //    QFile card(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + dfsStruct::ACTOR_CARD_FILE);
    //    if (!card.exists())
    //        return {};
    //    card.open(QIODevice::ReadOnly);
    //    QList<QByteArray> list =
    //        Serialization::deserialize(card.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    //    if (list.isEmpty())
    //        return dfsStruct::service;
    //    for (const QByteArray &el : list)

    //        if (path.toUtf8()
    //            == Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(0))
    //            return dfsStruct::convertToDFType(
    //                Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(3));
    QString pathLocal(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/');
    DBConnector dbConnect;
    QStringList listData;
    if (!dbConnect.open(pathLocal.toStdString() + dfsStruct::ACTOR_CARD_FILE.toStdString()))
    {
        qDebug() << "[Error][Card_Manager][getTypeByName] dimka nividimka";
        return dfsStruct::service;
    }
    QByteArray query = "SELECT type FROM ITEMS WHERE path=" + path.toUtf8() + ';';

    std::vector<DBRow> data = dbConnect.select(query.toStdString());
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
        + dfsStruct::toString(type) + "/" + (BigNumber(file.toLatin1()) / BigNumber(100)).toByteArray() + "/"
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
