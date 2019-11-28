#include "dfs/managers/headers/card_manager.h"

#include <QUrl>

QStringList CardManager::getFilesByType(const QString &userId, based_dfs_struct::Type type)
{
    QFile card(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + based_dfs_struct::ACTOR_CARD_FILE);
    if (!card.exists())
        return {};
    card.open(QIODevice::ReadOnly);
    QList<QByteArray> list =
        Serialization::deserialize(card.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    if (list.isEmpty())
        return QStringList();
    QStringList result;
    for (const QByteArray &el : list)
    {
        QByteArray dType =
            Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(3);
        if (based_dfs_struct::toByteArray(type) == dType)
            result << based_dfs_struct::ROOT_FOOLDER_NAME + "/" + userId + "/" + dType + "/"
                    + Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2);
    }
    return result;
}

QByteArray CardManager::getLastFileName(const QString &userId)
{
    QFile file(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + based_dfs_struct::ACTOR_CARD_FILE);
    if (!file.exists())
        return "";
    file.open(QIODevice::ReadOnly);

    QList<QByteArray> list =
        Serialization::deserialize(file.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    if (list.isEmpty())
        return "0";
    return Serialization::deserialize(list.takeLast(), Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2);
}

QStringList CardManager::getAllFiles(const QByteArray &userId)
{
    QFile card(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + based_dfs_struct::ACTOR_CARD_FILE);
    if (!card.exists())
        return {};
    card.open(QIODevice::ReadOnly);
    QList<QByteArray> list =
        Serialization::deserialize(card.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    if (list.isEmpty())
        return QStringList();
    QStringList result;
    for (const QByteArray &el : list)

        result << Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2);

    return result;
}

based_dfs_struct::Type CardManager::getTypeByName(const QString &path, const QByteArray &userId)
{
    QFile card(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + based_dfs_struct::ACTOR_CARD_FILE);
    if (!card.exists())
        return {};
    card.open(QIODevice::ReadOnly);
    QList<QByteArray> list =
        Serialization::deserialize(card.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    if (list.isEmpty())
        return based_dfs_struct::servic;
    for (const QByteArray &el : list)

        if (path.toUtf8()
            == Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2))
            return based_dfs_struct::convertToDFType(
                Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(3));
    return based_dfs_struct::servic;
}

QStringList CardManager::getAll(based_dfs_struct::Type type)
{
    QStringList all;

    const QStringList allUserIds =
        QDir(based_dfs_struct::ROOT_FOOLDER_NAME).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (auto &userId : allUserIds)
    {
        QStringList files = getFilesByType(userId.toLatin1(), type);
        all << files;
    }

    return all;
}

QString CardManager::buildPathForFile(const QString &userId, const QString &file, based_dfs_struct::Type type,
                                      bool localFormat)
{
    const QString currentPath = QUrl::fromLocalFile(QDir::currentPath()).toString() + "/";
    return (localFormat ? currentPath : "") + based_dfs_struct::ROOT_FOOLDER_NAME + "/" + userId + "/"
        + based_dfs_struct::toString(type) + "/" + (BigNumber(file.toLatin1()) / BigNumber(100)).toByteArray()
        + "/" + file;
}

QStringList CardManager::buildPathForFiles(const QString &userId, const QStringList &files,
                                           based_dfs_struct::Type type, bool localFormat)
{
    QStringList result;

    for (const QString &file : files)
    {
        result << buildPathForFile(userId, file, type, localFormat);
    }

    return result;
}
