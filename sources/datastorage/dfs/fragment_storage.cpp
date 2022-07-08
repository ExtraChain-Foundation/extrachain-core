#include "datastorage/dfs/fragment_storage.h"
#include "datastorage/dfs/historical_chain.h"
#include "utils/dfs_utils.h"

#include <fstream>

FragmentStorage::FragmentStorage(ActorId Actor, std::string FileName, std::string FileHash)
    : storageFile(DFS_PATH::filePath(Actor, FileName).string() + DFSF::Extension) {
    actor = Actor;
    fileName = FileName;
    fileHash = FileHash;
    storageFile.open();
    storageFile.query(DFSF::CreateTableQueryFragments);
}

FragmentStorage::FragmentStorage(DFS::Packets::SegmentMessage segmentMessage)
    : storageFile(DFS_PATH::filePath(segmentMessage.Actor, segmentMessage.FileName).string()
                  + DFSF::Extension)
    , actor(segmentMessage.Actor)
    , fileName(segmentMessage.FileName)
    , fileHash(segmentMessage.FileHash) {
    storageFile.open();
    storageFile.query(DFSF::CreateTableQueryFragments);
}

bool FragmentStorage::initLocalFile(uint64_t filesize) {
    DBRow row = makeFragmentRow(0, 0, filesize);
    return storageFile.insert(DFSF::TableNameFragments, row);
}

bool FragmentStorage::initHistoricalChain() {
    HistoricalChain hc(storageFile.file(), DFS_PATH::filePath(actor, fileName).string());
    return hc.initLocal(actor.toStdString(), fileName, fileHash);
}

bool FragmentStorage::insertFragment(DFSP::SegmentMessage msg) {
    uint64_t pos = writeFragment(msg);
    DBRow row = makeFragmentRow(msg, pos);
    const auto inserted = storageFile.insert(DFSF::TableNameFragments, row);
    moveRows(row, msg.Data.size());
    return inserted;
}

bool FragmentStorage::editFragment(DFSP::EditSegmentMessage msg) {
    const auto fileHash = msg.NewFileHash.empty() ? msg.FileHash : msg.NewFileHash;

    switch (msg.ActionType) {
    case DFSP::SegmentMessageType::insert: {
        //        checkRenameFile(msg);
        return insertFragment(DFSP::SegmentMessage { .Actor = msg.Actor,
                                                     .FileName = msg.FileName,
                                                     .FileHash = msg.FileHash,
                                                     .Data = msg.Data,
                                                     .Offset = msg.Offset });
    }
    case DFSP::SegmentMessageType::add: {
        //        checkRenameFile(msg);
        return insertFragment(DFSP::SegmentMessage { .Actor = msg.Actor,
                                                     .FileName = msg.FileName,
                                                     .FileHash = msg.FileHash,
                                                     .Data = msg.Data,
                                                     .Offset = msg.Offset });
    }
    case DFSP::SegmentMessageType::replace: {
        std::filesystem::path filePath = DFS::Path::filePath(actor.toStdString(), fileName);
        HistoricalChain historicalChain(storageFile.file(), filePath.string());
        historicalChain.apply(msg);
        return applyChanges(msg.Data, msg.Offset);
    }
    case DFSP::SegmentMessageType::remove: {
        //        checkRenameFile(msg);
        std::filesystem::path realFilePath = DFS_PATH::filePath(msg.Actor, fileHash);
        boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
        auto fileSize = std::filesystem::file_size(realFilePath);

        return removeFragment(DFSP::DeleteSegmentMessage { .Actor = msg.Actor,
                                                           .FileName = msg.FileName,
                                                           .FileHash = msg.FileHash,
                                                           .Offset = msg.Offset,
                                                           .Size = fileSize });
    }
    }
    return false;
}

bool FragmentStorage::removeFragment(DFSP::DeleteSegmentMessage msg) {
    std::string GetStartFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments
        + " WHERE pos = " + std::to_string(msg.Offset) + " ORDER BY pos DESC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DBRow frag = array[0];
        storageFile.deleteRow(DFSF::TableNameFragments, frag);
        std::filesystem::path filePath = DFS_PATH::filePath(actor, fileName);

        HistoricalChain historicalChain(storageFile.file(), filePath.string());
        DFSP::EditSegmentMessage editSegmentMessage =
            historicalChain.makeEditSegmentMessage(msg, DFSP::SegmentMessageType::remove);
        historicalChain.apply(editSegmentMessage);

        return remove(filePath, std::stoull(frag.at("storedPos")), std::stoull(frag.at("size")));
    }
    return false;
}

DFSP::SegmentMessage FragmentStorage::getFragment(uint64_t pos) {
    DFSP::SegmentMessage fragment;

    std::string GetStartFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments
        + " WHERE pos = " + std::to_string(pos) + " ORDER BY pos DESC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DBRow fragMap = array[0];
        std::filesystem::path filePath = DFS_PATH::filePath(actor, fileName);
        fragment.Offset = pos;
        fragment.Data = extract(filePath, pos, std::stoull(fragMap.at("size")));
        fragment.Actor = this->actor.toStdString();
        fragment.FileHash = this->fileName;
        return fragment;
    }
    return fragment;
}

bool FragmentStorage::applyChanges(const std::string &data, uint64_t pos) {
    if (data.empty()) {
        qFatal("Where I took a wrong turn");
    }

    uint64_t endPos = pos + data.length();
    auto filePath = DFS_PATH::filePath(actor, fileName);
    std::vector<DBRow> frags =
        storageFile.select("SELECT * FROM " + DFSF::TableNameFragments + " WHERE pos + size > "
                           + std::to_string(pos) + " AND pos < " + std::to_string(endPos));

    for (int i = 0; i < frags.size(); i++) {
        uint64_t fragpos = std::stoull(frags[i].at("pos"));
        uint64_t fragsize = std::stoull(frags[i].at("size"));
        uint64_t fragposend = fragpos + fragsize;
        uint64_t fragstored = std::stoull(frags[i].at("storedPos"));

        if (fragpos > pos && fragposend < endPos) { // middle
            remove(filePath, fragstored, fragsize);
            write(filePath, fragstored, data.substr(fragpos - pos, fragsize));
        } else if (fragposend > endPos && fragpos > pos) { // end
            remove(filePath, fragstored, endPos - fragpos);
            write(filePath, fragstored, data.substr(fragpos - pos, endPos - fragpos));
        } else { // begin
            auto storedpos = pos - (fragpos - fragstored);
            auto count = std::min<unsigned long>(data.length(), fragposend - pos);
            remove(filePath, storedpos, count);
            write(filePath, storedpos, data.substr(0, count));
        }
    }

    return true;
}

DBRow FragmentStorage::getPreviousFragment(uint64_t number) {
    DBRow ret;
    std::string GetPrevFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE pos < "
        + std::to_string(number) + " ORDER BY pos DESC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetPrevFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DBRow FragmentStorage::getNextFragment(uint64_t number) {
    DBRow ret;
    std::string GetNextFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE pos > "
        + std::to_string(number) + " ORDER BY pos ASC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetNextFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DBRow FragmentStorage::getRealPreviousFragment(uint64_t number) {
    DBRow ret;
    std::string GetPrevFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE storedPos < "
        + std::to_string(number) + " ORDER BY pos DESC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetPrevFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DBRow FragmentStorage::getRealNextFragment(uint64_t number) {
    DBRow ret;
    std::string GetNextFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE storedPos > "
        + std::to_string(number) + " ORDER BY pos ASC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetNextFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

std::pair<DBRow, DBRow> FragmentStorage::getPrevNextPairFragment(uint64_t number) {
    std::vector<DBRow> res;
    std::pair<DBRow, DBRow> ret;
    std::string GetPrevFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE pos < "
        + std::to_string(number) + " ORDER BY pos DESC LIMIT 1";
    res = storageFile.select(GetPrevFragmentQuery);
    if (!res.empty()) {
        ret.first = res[0];
    }
    std::string GetNextFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE pos > "
        + std::to_string(number) + " ORDER BY pos ASC LIMIT 1";
    res = storageFile.select(GetNextFragmentQuery);
    if (!res.empty()) {
        ret.second = res[0];
    }
    return ret;
}

DBRow FragmentStorage::makeFragmentRow(DFSP::SegmentMessage msg, uint64_t storedPos) {
    DBRow row;
    row.insert({ "pos", std::to_string(msg.Offset) });
    row.insert({ "storedPos", std::to_string(storedPos) });
    row.insert({ "size", std::to_string(msg.Data.size()) });
    // row.insert({ "fragHash", Utils::calcHash(msg.Data) });
    return row;
}

DBRow FragmentStorage::makeFragmentRow(uint64_t pos, uint64_t storedPos, uint64_t size) {
    DBRow row;
    row.insert({ "pos", std::to_string(pos) });
    row.insert({ "storedPos", std::to_string(storedPos) });
    row.insert({ "size", std::to_string(size) });
    return row;
}

uint64_t FragmentStorage::writeFragment(DFSP::SegmentMessage msg) {
    std::filesystem::path filePath = DFS_PATH::filePath(actor, fileName);
    std::pair<DBRow, DBRow> prevnext = getPrevNextPairFragment(msg.Offset);
    uint64_t posToWrite = 0;
    if (prevnext.first.empty()) {
        posToWrite = 0;
    } else if (prevnext.second.empty()) {
        posToWrite = std::filesystem::file_size(filePath);
    } else {
        posToWrite = std::stoull(prevnext.second["storedPos"]);
    }
    return write(filePath, posToWrite, msg.Data);
}

void FragmentStorage::moveRows(DBRow curRow, uint64_t moveSize) {
    uint64_t curPos = std::stoull(curRow["storedPos"]);
    DBRow nextFragment = getNextFragment(std::stoull(curRow["pos"]));
    if (nextFragment.empty()) {
        return;
    } else {
        storageFile.update("UPDATE " + DFSF::TableNameFragments + " SET storedPos = '"
                           + std::to_string(curPos + moveSize) + "' WHERE pos = '" + nextFragment["pos"]
                           + "'");
        moveRows(nextFragment, moveSize);
    }
}

uint64_t FragmentStorage::write(std::filesystem::path filePath, uint64_t pos, std::string data) {
    uint64_t fz = std::filesystem::file_size(filePath);
    if (pos == fz) {
        std::ofstream ofs(filePath.string(), std::ios::binary | std::ios::app);
        ofs << data;
        ofs.close();
        return pos;
    }
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + filePath.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + filePath.stem().string();
    std::ofstream ofs(tempFilePath.string(), std::ios::binary);
    boost::interprocess::file_mapping fmapSource(filePath.c_str(), boost::interprocess::read_write);
    ofs.write(data.c_str(), data.size()); // add data to new temp file
    ofs.flush();
    std::size_t i = 0;

    for (i = pos; i < fz; i = i + DFSB::sectionSize) { // copy old data to new temp file
        if (i + DFSB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i,
                                                           DFSB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
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
        uint64_t fzres = std::filesystem::file_size(tempFilePath);

        for (i = 0; i < fzres; i = i + DFSB::sectionSize) { // copy new data to old file
            if (i + DFSB::sectionSize < fzres) {
                boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i,
                                                               DFSB::sectionSize);
                char *rr_ptr = static_cast<char *>(rightRegion.get_address());
                ofsres.write(rr_ptr, rightRegion.get_size());
            } else {
                boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write,
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

std::string FragmentStorage::extract(std::filesystem::path filePath, uint64_t pos, uint64_t size) {
    boost::interprocess::file_mapping fmapSource(filePath.c_str(), boost::interprocess::read_only);
    boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_only, pos, size);
    char *rr_ptr = static_cast<char *>(rightRegion.get_address());
    std::string str(rr_ptr, size);
    return str;
}

uint64_t FragmentStorage::remove(std::filesystem::path filePath, uint64_t pos, uint64_t size) {
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + filePath.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + filePath.stem().string();
    std::ofstream ofs(tempFilePath.string());
    if (!ofs.is_open()) {
        return false;
    }
    boost::interprocess::file_mapping fmapSource(filePath.c_str(), boost::interprocess::read_write);
    uint64_t fz = std::filesystem::file_size(filePath);
    std::size_t i = 0;
    for (i = pos + size; i < fz; i = i + DFSB::sectionSize) { // copy old data to new temp file
        if (i + DFSB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i,
                                                           DFSB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        }
    }
    ofs.close();

    std::filesystem::resize_file(filePath, pos); // cut right side from old file
    std::ofstream ofsres(filePath.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    uint64_t fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DFSB::sectionSize) { // copy new data to old file
        if (i + DFSB::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i,
                                                           DFSB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        } else {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
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

    fileHash = msg.NewFileHash;
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::path path = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim;
    std::filesystem::rename(path / std::string(msg.FileHash), path / std::string(msg.NewFileHash));
    return std::filesystem::exists(path / std::string(msg.NewFileHash));
}

FragmentWriter::FragmentWriter(const DFS::Packets::SegmentMessage &msg,
                               std::vector<std::string> compliteFiles, QObject *parent)
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
    actrDirData = actrDirFile.select("SELECT * FROM " + DFST::ActorDirFile::TableName + " WHERE fileName = '"
                                     + m_msg.FileName + "';");
    std::string virtualPath = actrDirData[0].at("filePath");
    uint64_t fileSize = std::stoull(actrDirData[0].at("fileSize"));
    auto currentFileSize = std::filesystem::file_size(fileName);
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
            fs.initHistoricalChain();
            qDebug() << "File " << fileName.c_str() << " downloaded";
        } else {
            emit requestFile(m_msg.Actor, m_msg.FileName);
        }
    }
}
