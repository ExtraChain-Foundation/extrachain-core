#include "dfs/types/headers/cardfile.h"

#include <QFileInfo>

CardFile::CardFile(QString userId)
{
    m_userId = userId;
    m_fileName = QString("%1/%2/%3").arg(DfsStruct::ROOT_FOOLDER_NAME, userId, DfsStruct::ACTOR_CARD_FILE);
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

std::optional<DBRow> CardFile::last()
{
    auto result = m_db.select("SELECT * FROM Items ORDER by _rowid_ DESC LIMIT 1"); // WHERE nextId = '-'

    if (!result.empty())
        return result[0];

    return {};
}
