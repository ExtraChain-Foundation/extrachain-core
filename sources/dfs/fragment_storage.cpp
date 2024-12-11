/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#include "dfs/fragment_storage.h"
#include "dfs/historical_chain.h"
#include "dfs/dfs_utils.h"

#include <fstream>

FragmentStorage::FragmentStorage(ActorId actorId, std::string fileId, std::string hash)
    : fragments_file(DfsPath::filePath(actorId, fileId).string() + DfsF::Extension) {
    this->actor_id = actorId;
    this->file_id  = fileId;
    this->hash    = hash;
    fragments_file.open();
    fragments_file.query(DfsF::CreateTableQueryFragments);
}

FragmentStorage::FragmentStorage(Dfs::Packets::SegmentMessage segmentMessage)
    : fragments_file(DfsPath::filePath(segmentMessage.actorId, segmentMessage.file_id).string() + DfsF::Extension)
    , actor_id(segmentMessage.actorId)
    , file_id(segmentMessage.file_id)
    , hash(segmentMessage.hash) {
    fragments_file.open();
    fragments_file.query(DfsF::CreateTableQueryFragments);
}

bool FragmentStorage::initLocalFile(std::uint64_t filesize) {
    DbRow row = makeFragmentRow(0, 0, filesize);
    return fragments_file.insert(DfsF::TableNameFragments, row);
}

bool FragmentStorage::initHistoricalChain() {
    HistoricalChain hc(fragments_file.file(), DfsPath::filePath(actor_id, file_id).string());
    return hc.initLocal(actor_id, file_id, hash);
}

bool FragmentStorage::insertFragment(DfsP::SegmentMessage msg) {
    std::uint64_t pos      = writeFragment(msg);
    DbRow         row      = makeFragmentRow(msg, pos);
    const auto    inserted = fragments_file.replace(DfsF::TableNameFragments, row);
    moveRows(row, msg.data.size());
    return inserted;
}

bool FragmentStorage::editFragment(DfsP::EditSegmentMessage msg) {
    const auto fileHash = msg.newHash.empty() ? msg.hash : msg.newHash;

    switch (msg.actionType) {
    case DfsP::SegmentMessageType::Insert: {
        //        checkRenameFile(msg);
        return insertFragment(DfsP::SegmentMessage { .actorId = msg.actorId,
                                                     .file_id  = msg.file_id,
                                                     .hash    = msg.hash,
                                                     .data    = msg.data,
                                                     .offset  = msg.offset });
    }
    case DfsP::SegmentMessageType::Add: {
        //        checkRenameFile(msg);
        return insertFragment(DfsP::SegmentMessage { .actorId = msg.actorId,
                                                     .file_id  = msg.file_id,
                                                     .hash    = msg.hash,
                                                     .data    = msg.data,
                                                     .offset  = msg.offset });
    }
    case DfsP::SegmentMessageType::Replace: {
        std::filesystem::path filePath = Dfs::Path::filePath(actor_id, file_id);
        HistoricalChain       historicalChain(fragments_file.file(), filePath.string());
        historicalChain.apply(msg);
        return applyChanges(msg.data, msg.offset);
    }
    case DfsP::SegmentMessageType::Remove: {
        //        checkRenameFile(msg);
        std::filesystem::path             realFilePath = DfsPath::filePath(msg.actorId, fileHash);
        boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
        auto                              fileSize = std::filesystem::file_size(realFilePath);

        return removeFragment(DfsP::DeleteSegmentMessage { .actorId = msg.actorId,
                                                           .file_id  = msg.file_id,
                                                           .hash    = msg.hash,
                                                           .offset  = msg.offset,
                                                           .size    = fileSize });
    }
    }
    return false;
}

bool FragmentStorage::removeFragment(DfsP::DeleteSegmentMessage msg) {
    std::string GetStartFragmentQuery = fmt::format("SELECT * FROM {} WHERE pos = {} ORDER BY pos DESC LIMIT 1",
                                                    DfsF::TableNameFragments,
                                                    std::to_string(msg.offset));
    std::vector<DbRow> array          = fragments_file.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DbRow frag = array[0];
        fragments_file.delete_row(DfsF::TableNameFragments, frag);
        std::filesystem::path filePath = DfsPath::filePath(actor_id, file_id);

        HistoricalChain          historicalChain(fragments_file.file(), filePath.string());
        DfsP::EditSegmentMessage editSegmentMessage =
            historicalChain.makeEditSegmentMessage(msg, DfsP::SegmentMessageType::Remove);
        historicalChain.apply(editSegmentMessage);

        return remove(filePath, std::stoull(frag.at("storedPos")), std::stoull(frag.at("size")));
    }
    return false;
}

DfsP::SegmentMessage FragmentStorage::getFragment(std::uint64_t pos) {
    DfsP::SegmentMessage fragment;

    std::string GetStartFragmentQuery = fmt::format("SELECT * FROM {} WHERE pos = {} ORDER BY pos DESC LIMIT 1",
                                                    DfsF::TableNameFragments,
                                                    std::to_string(pos));
    std::vector<DbRow> array          = fragments_file.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DbRow                 fragMap  = array[0];
        std::filesystem::path filePath = DfsPath::filePath(actor_id, file_id);
        fragment.offset                = pos;
        fragment.data                  = extract(filePath, pos, std::stoull(fragMap.at("size")));
        fragment.actorId               = this->actor_id.to_string();
        fragment.hash                  = this->file_id;
        return fragment;
    }
    return fragment;
}

Dfs::Packets::SegmentMessage FragmentStorage::getFragment(std::string fragHash) {
    DfsP::SegmentMessage fragment;

    std::string GetStartFragmentQuery =
        fmt::format("SELECT * FROM {} WHERE fragHash = '{}' ORDER BY pos DESC LIMIT 1",
                    DfsF::TableNameFragments,
                    fragHash);
    std::vector<DbRow> array = fragments_file.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DbRow                 fragMap  = array[0];
        std::filesystem::path filePath = DfsPath::filePath(actor_id, file_id);
        fragment.offset                = std::stoull(fragMap.at("pos"));
        fragment.data    = extract(filePath, std::stoull(fragMap.at("pos")), std::stoull(fragMap.at("size")));
        fragment.actorId = this->actor_id.to_string();
        fragment.hash    = fragMap.at("fragHash");
    }
    return fragment;
}

bool FragmentStorage::applyChanges(const std::string &data, std::uint64_t pos) {
    if (data.empty()) {
        eFatal("Where I took a wrong turn");
    }

    std::uint64_t      endPos   = pos + data.length();
    auto               filePath = DfsPath::filePath(actor_id, file_id);
    std::vector<DbRow> frags =
        fragments_file.select(fmt::format("SELECT * FROM {} WHERE pos + size > {} AND pos < {}",
                                       DfsF::TableNameFragments,
                                       std::to_string(pos),
                                       std::to_string(endPos)));

    for (int i = 0; i < frags.size(); i++) {
        std::uint64_t fragpos    = std::stoull(frags[i].at("pos"));
        std::uint64_t fragsize   = std::stoull(frags[i].at("size"));
        std::uint64_t fragposend = fragpos + fragsize;
        std::uint64_t fragstored = std::stoull(frags[i].at("storedPos"));

        if (fragpos > pos && fragposend < endPos) { // middle
            remove(filePath, fragstored, fragsize);
            write(filePath, fragstored, data.substr(fragpos - pos, fragsize));
        } else if (fragposend > endPos && fragpos > pos) { // end
            remove(filePath, fragstored, endPos - fragpos);
            write(filePath, fragstored, data.substr(fragpos - pos, endPos - fragpos));
        } else { // begin
            auto storedpos = pos - (fragpos - fragstored);
            auto count     = std::min<unsigned long>(data.length(), fragposend - pos);
            remove(filePath, storedpos, count);
            write(filePath, storedpos, data.substr(0, count));
        }
    }

    return true;
}

DbRow FragmentStorage::getPreviousFragment(std::uint64_t number) {
    DbRow       ret;
    std::string GetPrevFragmentQuery = fmt::format("SELECT * FROM {} WHERE pos < {} ORDER BY pos DESC LIMIT 1",
                                                   DfsF::TableNameFragments,
                                                   std::to_string(number));
    std::vector<DbRow> array         = fragments_file.select(GetPrevFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DbRow FragmentStorage::getNextFragment(std::uint64_t number) {
    DbRow       ret;
    std::string GetNextFragmentQuery = fmt::format("SELECT * FROM {} WHERE pos > {} ORDER BY pos ASC LIMIT 1",
                                                   DfsF::TableNameFragments,
                                                   std::to_string(number));
    std::vector<DbRow> array         = fragments_file.select(GetNextFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DbRow FragmentStorage::getRealPreviousFragment(std::uint64_t number) {
    DbRow       ret;
    std::string GetPrevFragmentQuery =
        fmt::format("SELECT * FROM {} WHERE storedPos < {} ORDER BY pos DESC LIMIT 1",
                    DfsF::TableNameFragments,
                    std::to_string(number));
    std::vector<DbRow> array = fragments_file.select(GetPrevFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DbRow FragmentStorage::getRealNextFragment(std::uint64_t number) {
    DbRow       ret;
    std::string GetNextFragmentQuery =
        fmt::format("SELECT * FROM {} WHERE storedPos > {} ORDER BY pos ASC LIMIT 1",
                    DfsF::TableNameFragments,
                    std::to_string(number));
    std::vector<DbRow> array = fragments_file.select(GetNextFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

std::pair<DbRow, DbRow> FragmentStorage::getPrevNextPairFragment(std::uint64_t number) {
    std::vector<DbRow>      res;
    std::pair<DbRow, DbRow> ret;
    std::string GetPrevFragmentQuery = fmt::format("SELECT * FROM {} WHERE pos < {} ORDER BY pos DESC LIMIT 1",
                                                   DfsF::TableNameFragments,
                                                   std::to_string(number));
    res                              = fragments_file.select(GetPrevFragmentQuery);
    if (!res.empty()) {
        ret.first = res[0];
    }
    std::string GetNextFragmentQuery = fmt::format("SELECT * FROM {} WHERE pos > {} ORDER BY pos ASC LIMIT 1",
                                                   DfsF::TableNameFragments,
                                                   std::to_string(number));
    res                              = fragments_file.select(GetNextFragmentQuery);
    if (!res.empty()) {
        ret.second = res[0];
    }
    return ret;
}

DbRow FragmentStorage::makeFragmentRow(DfsP::SegmentMessage msg, std::uint64_t storedPos) {
    DbRow row;
    row.insert({ "pos", std::to_string(msg.offset) });
    row.insert({ "storedPos", std::to_string(storedPos) });
    row.insert({ "size", std::to_string(msg.data.size()) });
    row.insert({ "fragHash", Utils::calculate_hash(msg.data) });
    return row;
}

DbRow FragmentStorage::makeFragmentRow(std::uint64_t pos, std::uint64_t storedPos, std::size_t size) {
    DbRow row;
    row.insert({ "pos", std::to_string(pos) });
    row.insert({ "storedPos", std::to_string(storedPos) });
    row.insert({ "size", std::to_string(size) });
    row.insert({ "fragHash", hash });

    return row;
}

std::uint64_t FragmentStorage::writeFragment(DfsP::SegmentMessage msg) {
    std::filesystem::path   filePath   = DfsPath::filePath(actor_id, file_id);
    std::pair<DbRow, DbRow> prevnext   = getPrevNextPairFragment(msg.offset);
    std::uint64_t           posToWrite = 0;
    if (prevnext.first.empty()) {
        posToWrite = 0;
    } else if (prevnext.second.empty()) {
        posToWrite = std::filesystem::file_size(filePath);
    } else {
        posToWrite = std::stoull(prevnext.second["storedPos"]);
    }
    return write(filePath, posToWrite, msg.data);
}

void FragmentStorage::moveRows(DbRow curRow, std::uint64_t moveSize) {
    std::uint64_t curPos       = std::stoull(curRow["storedPos"]);
    DbRow         nextFragment = getNextFragment(std::stoull(curRow["pos"]));
    if (nextFragment.empty()) {
        return;
    } else {
        fragments_file.update(fmt::format("UPDATE {} SET storedPos = '{}' WHERE pos = '{}'",
                                       DfsF::TableNameFragments,
                                       std::to_string(curPos + moveSize),
                                       nextFragment["pos"]));
        moveRows(nextFragment, moveSize);
    }
}

std::uint64_t FragmentStorage::write(std::filesystem::path filePath, std::uint64_t pos, std::string data) {
    std::uint64_t fz = std::filesystem::file_size(filePath);
    if (pos == fz) {
        std::ofstream ofs(filePath.string(), std::ios::binary | std::ios::app);
        ofs << data;
        ofs.close();
        return pos;
    }
    std::string           pathDelim    = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + filePath.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + filePath.stem().string();
    std::ofstream                     ofs(tempFilePath.string(), std::ios::binary);
    boost::interprocess::file_mapping fmapSource(filePath.c_str(), boost::interprocess::read_write);
    ofs.write(data.c_str(), data.size()); // add data to new temp file
    ofs.flush();
    std::size_t i = 0;

    for (i = pos; i < fz; i = i + DfsB::sectionSize) { // copy old data to new temp file
        if (i + DfsB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource,
                                                           boost::interprocess::read_write,
                                                           i,
                                                           DfsB::sectionSize);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        }
    }
    ofs.close();
    if (pos == 0) {
        std::filesystem::remove(filePath);
        std::filesystem::copy_file(tempFilePath, filePath);
    } else {
        std::filesystem::resize_file(filePath, pos); // cut right side from old file
        std::ofstream ofsres(filePath.c_str(), std::ios::out | std::ios::app | std::ios::binary);
        boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
        std::uint64_t                     fzres = std::filesystem::file_size(tempFilePath);

        for (i = 0; i < fzres; i = i + DfsB::sectionSize) { // copy new data to old file
            if (i + DfsB::sectionSize < fzres) {
                boost::interprocess::mapped_region rightRegion(fmapTarget,
                                                               boost::interprocess::read_write,
                                                               i,
                                                               DfsB::sectionSize);
                char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
                ofsres.write(rr_ptr, rightRegion.get_size());
            } else {
                boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i);
                char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
                ofsres.write(rr_ptr, rightRegion.get_size());
            }
        }
        ofsres.close();
    }

    std::filesystem::remove(tempFilePath);
    return pos;
}

std::string FragmentStorage::extract(std::filesystem::path filePath, std::uint64_t pos, std::size_t size) {
    boost::interprocess::file_mapping  fmapSource(filePath.c_str(), boost::interprocess::read_only);
    boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_only, pos, size);
    char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
    std::string                        str(rr_ptr, size);
    return str;
}

std::uint64_t FragmentStorage::remove(std::filesystem::path filePath, std::uint64_t pos, std::size_t size) {
    std::string           pathDelim    = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + filePath.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + filePath.stem().string();
    std::ofstream ofs(tempFilePath.string());
    if (!ofs.is_open()) {
        return false;
    }
    boost::interprocess::file_mapping fmapSource(filePath.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fz = std::filesystem::file_size(filePath);
    std::size_t                       i  = 0;
    for (i = pos + size; i < fz; i = i + DfsB::sectionSize) { // copy old data to new temp file
        if (i + DfsB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource,
                                                           boost::interprocess::read_write,
                                                           i,
                                                           DfsB::sectionSize);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        }
    }
    ofs.close();

    std::filesystem::resize_file(filePath, pos); // cut right side from old file
    std::ofstream                     ofsres(filePath.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DfsB::sectionSize) { // copy new data to old file
        if (i + DfsB::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget,
                                                           boost::interprocess::read_write,
                                                           i,
                                                           DfsB::sectionSize);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        } else {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        }
    }
    ofsres.close();

    std::filesystem::remove(tempFilePath);

    return true;
}

bool FragmentStorage::checkRenameFile(const Dfs::Packets::EditSegmentMessage &msg) {
    if (msg.newHash.empty())
        return false;

    hash                            = msg.newHash;
    std::string           pathDelim = Utils::platformDelimeter();
    std::filesystem::path path      = DfsB::fsActrRoot + pathDelim + msg.actorId.to_string() + pathDelim;
    std::filesystem::rename(path / std::string(msg.hash), path / std::string(msg.newHash));
    return std::filesystem::exists(path / std::string(msg.newHash));
}

FragmentWriter::FragmentWriter(const Dfs::Packets::SegmentMessage &msg,
                               std::vector<std::string>            compliteFiles,
                               QObject                            *parent)
    : QThread(parent)
    , m_msg(msg)
    , m_compliteFiles(compliteFiles) {
}

void FragmentWriter::run() {
    auto fileName = DfsPath::filePath(m_msg.actorId, m_msg.file_id);
    if (!std::filesystem::exists(fileName)
        || std::find(m_compliteFiles.begin(), m_compliteFiles.end(), m_msg.file_id) != m_compliteFiles.end()) {
        return;
    }

    auto dirRowExp = Dfs::Tables::ActorDirFile::get_dir_row(m_msg.actorId, m_msg.file_id);

    if (!dirRowExp.has_value()) {
        eLog("[Dfs] Fragments: dir row error");
        return;
    }

    auto dirRow = dirRowExp.value();

    std::string   virtualPath     = dirRow.visual_path();
    std::uint64_t fileSize        = dirRow.size;
    auto          currentFileSize = std::filesystem::file_size(fileName);
    if (fileSize == currentFileSize) {
        eLog("[Dfs] File is complite");
        emit compliteFile(m_msg.file_id);
        return;
    }

    FragmentStorage fs(m_msg);
    fs.insertFragment(m_msg);
    currentFileSize = std::filesystem::file_size(fileName);
    emit downloadProgress(m_msg.actorId, m_msg.file_id, double(m_msg.offset) / double(fileSize) * 100);
    if (fileSize == currentFileSize) {
        if (m_msg.hash == Utils::calculate_hash_file(FsPath::create(fileName).value()).value()) {
            eLog("[Dfs] File {} done", fileName);
            emit eraseFromFiles(m_msg);
            emit downloadedFile(dirRow);
            emit sendFile(m_msg.actorId, m_msg.file_id);
            //            fs.initHistoricalChain();
            eLog("File {} downloaded", fileName);
        } else {
            emit requestFile(m_msg.actorId, m_msg.file_id);
        }
    } else {
        DfsP::RequestFileSegmentMessage requestMsg {
            .actorId = m_msg.actorId,
            .file_id  = m_msg.file_id,
            .hash    = m_msg.hash,
            .offset  = m_msg.offset,
        };
        emit requestNextFragment(requestMsg);
    }
}
