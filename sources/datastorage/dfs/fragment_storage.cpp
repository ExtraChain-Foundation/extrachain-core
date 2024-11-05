#include "datastorage/dfs/fragment_storage.h"
#include "datastorage/dfs/historical_chain.h"
#include "utils/dfs_utils.h"

#include <fstream>

FragmentStorage::FragmentStorage(ActorId Actor, std::string FileName, std::string FileHash)
    : storageFile(DFS_PATH::filePath(Actor, FileName).string() + DFSF::Extension) {
    actor    = Actor;
    fileName = FileName;
    fileHash = FileHash;
    storageFile.open();
    storageFile.query(DFSF::CreateTableQueryFragments);
}

FragmentStorage::FragmentStorage(DFS::Packets::SegmentMessage segmentMessage)
    : storageFile(
          DFS_PATH::filePath(segmentMessage.Actor, segmentMessage.FileName).string() + DFSF::Extension)
    , actor(segmentMessage.Actor)
    , fileName(segmentMessage.FileName)
    , fileHash(segmentMessage.FileHash) {
    storageFile.open();
    storageFile.query(DFSF::CreateTableQueryFragments);
}

bool FragmentStorage::initLocalFile(std::uint64_t filesize) {
    DBRow row = makeFragmentRow(0, 0, filesize);
    return storageFile.insert(DFSF::TableNameFragments, row);
}

bool FragmentStorage::initHistoricalChain() {
    HistoricalChain hc(storageFile.file(), DFS_PATH::filePath(actor, fileName).string());
    return hc.initLocal(actor, fileName, fileHash);
}

bool FragmentStorage::insertFragment(DFSP::SegmentMessage msg) {
    std::uint64_t pos      = writeFragment(msg);
    DBRow         row      = makeFragmentRow(msg, pos);
    const auto    inserted = storageFile.insert(DFSF::TableNameFragments, row);
    moveRows(row, msg.Data.size());
    return inserted;
}

bool FragmentStorage::editFragment(DFSP::EditSegmentMessage msg) {
    const auto fileHash = msg.NewFileHash.empty() ? msg.FileHash : msg.NewFileHash;

    switch (msg.ActionType) {
    case DFSP::SegmentMessageType::insert: {
        //        checkRenameFile(msg);
        return insertFragment(DFSP::SegmentMessage { .Actor    = msg.Actor,
                                                     .FileName = msg.FileName,
                                                     .FileHash = msg.FileHash,
                                                     .Data     = msg.Data,
                                                     .Offset   = msg.Offset });
    }
    case DFSP::SegmentMessageType::add: {
        //        checkRenameFile(msg);
        return insertFragment(DFSP::SegmentMessage { .Actor    = msg.Actor,
                                                     .FileName = msg.FileName,
                                                     .FileHash = msg.FileHash,
                                                     .Data     = msg.Data,
                                                     .Offset   = msg.Offset });
    }
    case DFSP::SegmentMessageType::replace: {
        std::filesystem::path filePath = DFS::Path::filePath(actor, fileName);
        HistoricalChain       historicalChain(storageFile.file(), filePath.string());
        historicalChain.apply(msg);
        return applyChanges(msg.Data, msg.Offset);
    }
    case DFSP::SegmentMessageType::remove: {
        //        checkRenameFile(msg);
        std::filesystem::path             realFilePath = DFS_PATH::filePath(msg.Actor, fileHash);
        boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
        auto                              fileSize = std::filesystem::file_size(realFilePath);

        return removeFragment(DFSP::DeleteSegmentMessage { .Actor    = msg.Actor,
                                                           .FileName = msg.FileName,
                                                           .FileHash = msg.FileHash,
                                                           .Offset   = msg.Offset,
                                                           .Size     = fileSize });
    }
    }
    return false;
}

bool FragmentStorage::removeFragment(DFSP::DeleteSegmentMessage msg) {
    std::string GetStartFragmentQuery = fmt::format(
        "SELECT * FROM {} WHERE pos = {} ORDER BY pos DESC LIMIT 1",
        DFSF::TableNameFragments,
        std::to_string(msg.Offset));
    std::vector<DBRow> array = storageFile.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DBRow frag = array[0];
        storageFile.deleteRow(DFSF::TableNameFragments, frag);
        std::filesystem::path filePath = DFS_PATH::filePath(actor, fileName);

        HistoricalChain          historicalChain(storageFile.file(), filePath.string());
        DFSP::EditSegmentMessage editSegmentMessage =
            historicalChain.makeEditSegmentMessage(msg, DFSP::SegmentMessageType::remove);
        historicalChain.apply(editSegmentMessage);

        return remove(filePath, std::stoull(frag.at("storedPos")), std::stoull(frag.at("size")));
    }
    return false;
}

DFSP::SegmentMessage FragmentStorage::getFragment(std::uint64_t pos) {
    DFSP::SegmentMessage fragment;

    std::string GetStartFragmentQuery = fmt::format(
        "SELECT * FROM {} WHERE pos = {} ORDER BY pos DESC LIMIT 1",
        DFSF::TableNameFragments,
        std::to_string(pos));
    std::vector<DBRow> array = storageFile.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DBRow                 fragMap  = array[0];
        std::filesystem::path filePath = DFS_PATH::filePath(actor, fileName);
        fragment.Offset                = pos;
        fragment.Data                  = extract(filePath, pos, std::stoull(fragMap.at("size")));
        fragment.Actor                 = this->actor.toString();
        fragment.FileHash              = this->fileName;
        return fragment;
    }
    return fragment;
}

DFS::Packets::SegmentMessage FragmentStorage::getFragment(std::string fragHash) {
    DFSP::SegmentMessage fragment;

    std::string GetStartFragmentQuery = fmt::format(
        "SELECT * FROM {} WHERE fragHash = '{}' ORDER BY pos DESC LIMIT 1",
        DFSF::TableNameFragments,
        fragHash);
    std::vector<DBRow> array = storageFile.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DBRow                 fragMap  = array[0];
        std::filesystem::path filePath = DFS_PATH::filePath(actor, fileName);
        fragment.Offset                = std::stoull(fragMap.at("pos"));
        fragment.Data  = extract(filePath, std::stoull(fragMap.at("pos")), std::stoull(fragMap.at("size")));
        fragment.Actor = this->actor.toString();
        fragment.FileHash = fragMap.at("fragHash");
    }
    return fragment;
}

bool FragmentStorage::applyChanges(const std::string &data, std::uint64_t pos) {
    if (data.empty()) {
        qFatal("Where I took a wrong turn");
    }

    std::uint64_t      endPos   = pos + data.length();
    auto               filePath = DFS_PATH::filePath(actor, fileName);
    std::vector<DBRow> frags    = storageFile.select(fmt::format(
        "SELECT * FROM {} WHERE pos + size > {} AND pos < {}",
        DFSF::TableNameFragments,
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

DBRow FragmentStorage::getPreviousFragment(std::uint64_t number) {
    DBRow       ret;
    std::string GetPrevFragmentQuery = fmt::format(
        "SELECT * FROM {} WHERE pos < {} ORDER BY pos DESC LIMIT 1",
        DFSF::TableNameFragments,
        std::to_string(number));
    std::vector<DBRow> array = storageFile.select(GetPrevFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DBRow FragmentStorage::getNextFragment(std::uint64_t number) {
    DBRow       ret;
    std::string GetNextFragmentQuery = fmt::format(
        "SELECT * FROM {} WHERE pos > {} ORDER BY pos ASC LIMIT 1",
        DFSF::TableNameFragments,
        std::to_string(number));
    std::vector<DBRow> array = storageFile.select(GetNextFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DBRow FragmentStorage::getRealPreviousFragment(std::uint64_t number) {
    DBRow       ret;
    std::string GetPrevFragmentQuery = fmt::format(
        "SELECT * FROM {} WHERE storedPos < {} ORDER BY pos DESC LIMIT 1",
        DFSF::TableNameFragments,
        std::to_string(number));
    std::vector<DBRow> array = storageFile.select(GetPrevFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DBRow FragmentStorage::getRealNextFragment(std::uint64_t number) {
    DBRow       ret;
    std::string GetNextFragmentQuery = fmt::format(
        "SELECT * FROM {} WHERE storedPos > {} ORDER BY pos ASC LIMIT 1",
        DFSF::TableNameFragments,
        std::to_string(number));
    std::vector<DBRow> array = storageFile.select(GetNextFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

std::pair<DBRow, DBRow> FragmentStorage::getPrevNextPairFragment(std::uint64_t number) {
    std::vector<DBRow>      res;
    std::pair<DBRow, DBRow> ret;
    std::string             GetPrevFragmentQuery = fmt::format(
        "SELECT * FROM {} WHERE pos < {} ORDER BY pos DESC LIMIT 1",
        DFSF::TableNameFragments,
        std::to_string(number));
    res = storageFile.select(GetPrevFragmentQuery);
    if (!res.empty()) {
        ret.first = res[0];
    }
    std::string GetNextFragmentQuery = fmt::format(
        "SELECT * FROM {} WHERE pos > {} ORDER BY pos ASC LIMIT 1",
        DFSF::TableNameFragments,
        std::to_string(number));
    res = storageFile.select(GetNextFragmentQuery);
    if (!res.empty()) {
        ret.second = res[0];
    }
    return ret;
}

DBRow FragmentStorage::makeFragmentRow(DFSP::SegmentMessage msg, std::uint64_t storedPos) {
    DBRow row;
    row.insert({ "pos", std::to_string(msg.Offset) });
    row.insert({ "storedPos", std::to_string(storedPos) });
    row.insert({ "size", std::to_string(msg.Data.size()) });
    row.insert({ "fragHash", Utils::calcHash(msg.Data) });
    return row;
}

DBRow FragmentStorage::makeFragmentRow(std::uint64_t pos, std::uint64_t storedPos, std::size_t size) {
    DBRow row;
    row.insert({ "pos", std::to_string(pos) });
    row.insert({ "storedPos", std::to_string(storedPos) });
    row.insert({ "size", std::to_string(size) });
    row.insert({ "fragHash", fileHash });

    return row;
}

std::uint64_t FragmentStorage::writeFragment(DFSP::SegmentMessage msg) {
    std::filesystem::path   filePath   = DFS_PATH::filePath(actor, fileName);
    std::pair<DBRow, DBRow> prevnext   = getPrevNextPairFragment(msg.Offset);
    std::uint64_t           posToWrite = 0;
    if (prevnext.first.empty()) {
        posToWrite = 0;
    } else if (prevnext.second.empty()) {
        posToWrite = std::filesystem::file_size(filePath);
    } else {
        posToWrite = std::stoull(prevnext.second["storedPos"]);
    }
    return write(filePath, posToWrite, msg.Data);
}

void FragmentStorage::moveRows(DBRow curRow, std::uint64_t moveSize) {
    std::uint64_t curPos       = std::stoull(curRow["storedPos"]);
    DBRow         nextFragment = getNextFragment(std::stoull(curRow["pos"]));
    if (nextFragment.empty()) {
        return;
    } else {
        storageFile.update(fmt::format(
            "UPDATE {} SET storedPos = '{}' WHERE pos = '{}'",
            DFSF::TableNameFragments,
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

    for (i = pos; i < fz; i = i + DFSB::sectionSize) { // copy old data to new temp file
        if (i + DFSB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(
                fmapSource,
                boost::interprocess::read_write,
                i,
                DFSB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
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

        for (i = 0; i < fzres; i = i + DFSB::sectionSize) { // copy new data to old file
            if (i + DFSB::sectionSize < fzres) {
                boost::interprocess::mapped_region rightRegion(
                    fmapTarget,
                    boost::interprocess::read_write,
                    i,
                    DFSB::sectionSize);
                char *rr_ptr = static_cast<char *>(rightRegion.get_address());
                ofsres.write(rr_ptr, rightRegion.get_size());
            } else {
                boost::interprocess::mapped_region rightRegion(
                    fmapTarget,
                    boost::interprocess::read_write,
                    i);
                char *rr_ptr = static_cast<char *>(rightRegion.get_address());
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
    for (i = pos + size; i < fz; i = i + DFSB::sectionSize) { // copy old data to new temp file
        if (i + DFSB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(
                fmapSource,
                boost::interprocess::read_write,
                i,
                DFSB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
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
    std::ofstream ofsres(filePath.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DFSB::sectionSize) { // copy new data to old file
        if (i + DFSB::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(
                fmapTarget,
                boost::interprocess::read_write,
                i,
                DFSB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
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

bool FragmentStorage::checkRenameFile(const DFS::Packets::EditSegmentMessage &msg) {
    if (msg.NewFileHash.empty())
        return false;

    fileHash                        = msg.NewFileHash;
    std::string           pathDelim = Utils::platformDelimeter();
    std::filesystem::path path      = DFSB::fsActrRoot + pathDelim + msg.Actor.toString() + pathDelim;
    std::filesystem::rename(path / std::string(msg.FileHash), path / std::string(msg.NewFileHash));
    return std::filesystem::exists(path / std::string(msg.NewFileHash));
}

FragmentWriter::FragmentWriter(
    const DFS::Packets::SegmentMessage &msg,
    std::vector<std::string>            compliteFiles,
    QObject                            *parent)
    : QThread(parent)
    , m_msg(msg)
    , m_compliteFiles(compliteFiles) {
}

void FragmentWriter::run() {
    auto fileName = DFS_PATH::filePath(m_msg.Actor, m_msg.FileName);
    if (!std::filesystem::exists(fileName)
        || std::find(m_compliteFiles.begin(), m_compliteFiles.end(), m_msg.FileName)
               != m_compliteFiles.end()) {
        return;
    }

    DBConnector actrDirFile = DFST::ActorDirFile::actorDbConnector(m_msg.Actor);
    if (!actrDirFile.isOpen()) {
        qFatal("Error addFragment 1");
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByName(&actrDirFile, m_msg.FileName);
    actrDirData                    = actrDirFile.select(
        fmt::format("SELECT * FROM {} WHERE fileName = '{}'", DFST::ActorDirFile::TableName, m_msg.FileName));

    DBRow dirRowDb  = actrDirData[0];
    auto  dirRowExp = Utils::fromDbRow<DFS::DirRow>(dirRowDb);
    if (!dirRowExp.has_value()) {
        return;
    }
    auto dirRow = dirRowExp.value();

    std::string   virtualPath     = dirRow.visualPath();
    std::uint64_t fileSize        = dirRow.size;
    auto          currentFileSize = std::filesystem::file_size(fileName);
    if (fileSize == currentFileSize) {
        qDebug() << "[Dfs] File is complite";
        emit compliteFile(m_msg.FileName);
        return;
    }

    FragmentStorage fs(m_msg);
    fs.insertFragment(m_msg);
    currentFileSize = std::filesystem::file_size(fileName);
    emit downloadProgress(m_msg.Actor, m_msg.FileName, double(m_msg.Offset) / double(fileSize) * 100);
    if (fileSize == currentFileSize) {
        if (m_msg.FileHash == Utils::calcHashForFile(fileName)) {
            qDebug() << "[Dfs] File" << fileName.c_str() << "done";
            emit eraseFromFiles(m_msg);
            emit downloadedFile(m_msg.Actor, m_msg.FileName);
            emit sendFile(m_msg.Actor, m_msg.FileName);
            //            fs.initHistoricalChain();
            qDebug() << "File " << fileName.c_str() << " downloaded";
        } else {
            emit requestFile(m_msg.Actor, m_msg.FileName);
        }
    } else {
        DFSP::RequestFileSegmentMessage requestMsg {
            .Actor    = m_msg.Actor,
            .FileName = m_msg.FileName,
            .FileHash = m_msg.FileHash,
            .Offset   = m_msg.Offset,
        };
        emit requestNextFragment(requestMsg);
    }
}
