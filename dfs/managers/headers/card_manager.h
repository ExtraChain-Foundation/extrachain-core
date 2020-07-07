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

#ifndef CARD_MANAGER_H
#define CARD_MANAGER_H

#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"
#include "utils/db_connector.h"

class CardManager
{
public:
    enum class PathStyle
    {
        FullLocal,
        Root
    };

    static std::vector<std::string> getAll(DfsStruct::Type type);
    static std::vector<std::string> getFilesByType(const std::string &userId, DfsStruct::Type type);
    static QStringList getAllFiles(const QByteArray &userId);
    static DfsStruct::Type getTypeByName(const QString &fullPath);

    static std::string pathToRoot(std::string userId);
    static std::string buildPathForFile(const std::string &userId, const std::string &file,
                                        DfsStruct::Type type,
                                        PathStyle pathFormat = CardManager::PathStyle::Root);
    static std::vector<std::string> buildPathForFiles(const std::string &userId,
                                                      const std::vector<std::string> &files,
                                                      DfsStruct::Type type,
                                                      PathStyle pathFormat = CardManager::PathStyle::Root);

    static QString cutPath(QString fullPath);

    static int dfsVersion(QString path);

private:
    CardManager();
};

#endif // CARD_MANAGER_H
