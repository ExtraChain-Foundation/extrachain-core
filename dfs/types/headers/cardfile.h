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

#include <QFile>
#include <QString>

#include <optional>

#include "dfs/managers/headers/card_manager.h"
#include "dfs/types/headers/dfstruct.h"
#include "utils/db_connector.h"

class CardFile {
public:
    CardFile(QString userId);

    QString userId() const;
    QString fileName() const;
    bool isExists();
    bool open();
    bool close();

    std::optional<DBRow> last();

    bool append(QString fileId, int type, int version, QByteArray sign, bool isFilePath = false, int key = 1);
    bool updateLastCache();
    std::vector<DBRow> select(int count, int offset);

private:
    bool updateNextId();

    QString m_userId;
    QString m_fileName;
    QString m_lastCacheName;
    DBConnector m_db;
};
