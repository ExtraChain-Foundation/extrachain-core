#include "dfs/types/headers/cardfile.h"

#include <QFileInfo>

CardFile::CardFile(QString userId)
{
    m_userId = userId;
    m_fileName = QString("%1/%2/%3").arg(DfsStruct::ROOT_FOOLDER_NAME, userId, DfsStruct::ACTOR_CARD_FILE);
    m_lastCacheName = QString("%1/%2/root.last").arg(DfsStruct::ROOT_FOOLDER_NAME, userId);
}

QString CardFile::userId() const
{
    return m_userId;
}

QString CardFile::fileName() const
{
    return m_fileName;
}

bool CardFile::isExists()
{
    if (m_fileName.isEmpty())
        return false;

    QFileInfo dbFileInfo(m_fileName);
    if (!dbFileInfo.exists() || dbFileInfo.size() == 0)
        return false;

    return true;
}

bool CardFile::open()
{
    if (!isExists())
        return false;

    return m_db.open(m_fileName.toStdString());
}

bool CardFile::close()
{
    return m_db.close();
}

std::optional<DBRow> CardFile::last()
{
    auto result = m_db.select("SELECT * FROM " + Config::DataStorage::cardTableName
                              + " WHERE nextId = '-' ORDER by _rowid_ DESC LIMIT 1");

    if (!result.empty())
        return result[0];

    return {};
}

bool CardFile::append(QString fileId, int type, QByteArray sign, bool isFilePath)
{
    if (isFilePath)
        fileId = fileId.right(fileId.length() - fileId.lastIndexOf("/") - 1);

    std::string prevId = "-";
    auto lastRes = last();
    if (lastRes)
        prevId = lastRes.value()["id"];

    DBRow row;
    row.insert({
        "key",
        "auto_max",
    });
    row.insert({ "id", fileId.toStdString() });
    row.insert({ "type", std::to_string(type) });
    row.insert({ "prevId", prevId });
    row.insert({ "nextId", "-" });
    row.insert({ "sign", sign.toStdString() });

    QFile lastCacheFile(m_lastCacheName);
    if (lastCacheFile.open(QFile::WriteOnly))
    {
        lastCacheFile.write(fileId.toLatin1());
        lastCacheFile.close();
    }

    bool res = m_db.insert(Config::DataStorage::cardTableName, row);

    if (res && lastRes)
    {
        res = m_db.update("UPDATE " + Config::DataStorage::cardTableName + " SET nextId = '"
                          + fileId.toStdString() + "' WHERE id = '" + lastRes.value()["id"] + "'");
    }

    return res;
}

bool CardFile::updateLastCache()
{
    QFile lastCacheFile(m_lastCacheName);

    if (lastCacheFile.open(QFile::WriteOnly))
    {
        std::string fileId = "-";
        auto lastRes = last();
        if (lastRes)
            fileId = lastRes.value()["id"];

        if (!fileId.empty())
            lastCacheFile.write(fileId.c_str());
        else
            return false;
        lastCacheFile.close();

        return true;
    }

    return false;
}

std::vector<DBRow> CardFile::select(int count, int offset)
{
    return m_db.select("SELECT * FROM " + Config::DataStorage::cardTableName + " ORDER by key LIMIT "
                       + std::to_string(count) + " OFFSET " + std::to_string(offset));
}
