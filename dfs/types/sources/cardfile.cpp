/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "dfs/types/headers/cardfile.h"

#include <QFileInfo>

CardFile::CardFile(QString userId)
{
    m_userId = userId;
    m_fileName = QString("%1/%2/%3").arg(DfsStruct::ROOT_FOOLDER_NAME, userId, DfsStruct::ACTOR_CARD_FILE);
    m_lastCacheName =
        QString("%1/%2/%3").arg(DfsStruct::ROOT_FOOLDER_NAME, userId, DfsStruct::ACTOR_CARD_LAST);
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

bool CardFile::append(QString fileId, int type, int version, QByteArray sign, bool isFilePath, int key)
{
    if (isFilePath)
        fileId = CardManager::cutPath(fileId);

    /*
    if (type == 105 || type == 102)
    {
        int i = fileId.indexOf(".");
        fileId = fileId.mid(i - 2, 2) + "/" + fileId;
    }
    */

    QString filePath = QString::fromStdString(
        CardManager::buildPathForFile(m_userId.toStdString(), fileId.toStdString(), DfsStruct::Type(type)));

    std::string prevId = "-";
    auto lastRes = last();

    if (lastRes)
    {
        auto lastRes2 = *lastRes;
        prevId = lastRes2["id"];
        if (key == 1)
            key = std::stoi(lastRes2["key"]) + 1;
    }

    DBRow row;
    row.insert({ "key", std::to_string(key) });
    row.insert({ "id", fileId.toStdString() });
    row.insert({ "type", std::to_string(type) });
    row.insert({ "prevId", prevId });
    row.insert({ "nextId", "-" });
    row.insert({ "version", std::to_string(version) });
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
        auto lastRow = *lastRes;
        res = m_db.update("UPDATE " + Config::DataStorage::cardTableName + " SET nextId = '"
                          + fileId.toStdString() + "' WHERE id = '" + lastRow["id"] + "'");
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
        {
            auto lastRes2 = *lastRes;
            fileId = lastRes2["id"];
        }

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
