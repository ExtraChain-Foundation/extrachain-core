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

#pragma once

#include <filesystem>

#include "managers/extrachain_node.h"
#include "dfs/dfs_utils.h"

class EXTRACHAIN_EXPORT HistoricalChain {
private:
    std::filesystem::path objectPath;
    DbConnector           chainFile;

public:
    HistoricalChain(std::string chainFilePath, std::string objectFilePath);
    ~HistoricalChain();

public:
    bool                     apply(DfsP::EditSegmentMessage msg);
    bool                     remove(DfsP::EditSegmentMessage msg);
    bool                     revert(DfsP::EditSegmentMessage msg);
    bool                     update(DfsP::EditSegmentMessage msg, const int& num);
    DfsP::EditSegmentMessage getEditSegmentMessage(const int& num);
    DfsP::EditSegmentMessage getLastEditSegmentMessage();

    DfsP::EditSegmentMessage
    makeEditSegmentMessage(const DfsP::SegmentMessage& msg, const DfsP::SegmentMessageType& smType);
    DfsP::EditSegmentMessage
    makeEditSegmentMessage(const DfsP::DeleteSegmentMessage& msg, const DfsP::SegmentMessageType& smType);

    bool initLocal(const ActorId& actor, const std::string& fileName, const std::string& fileHash);
    bool remove(const ActorId& actor, const std::string& fileHash);
    bool rename(const std::string& fileHash, const std::string& newFileHash);

private:
    DbRow                    makeDBRow(std::uint64_t num, std::uint64_t prevNum, int type, std::string data);
    DbRow                    getLastRow();
    DbRow                    getNextRow(const int& currentNum);
    DbRow                    getRow(const int& num);
    DbRow                    getRow(const std::string& data);
    DfsP::EditSegmentMessage segmentMessageFromDBRow(const DbRow& dbRow);
};
