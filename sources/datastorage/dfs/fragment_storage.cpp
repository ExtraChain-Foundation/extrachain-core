#include "datastorage/dfs/fragment_storage.h"

FragmentStorage::FragmentStorage(ActorId Actor, std::string FileHash /*, DfsController *DFS*/) {
    actor = Actor;
    fileHash = FileHash;
    //    dfs = DFS;
    storageFile.open(actor.toStdString() + Utils::platformDelimeter() + fileHash + DFSF::Extension);
    storageFile.query(DFSF::CreateTableQueryFragments);
}

FragmentStorage::~FragmentStorage() {
    storageFile.close();
}

bool FragmentStorage::insertFragment(DFS::Packets::SegmentMessage msg) {
    uint64_t pos = writeFragment(msg);
    DBRow row = makeFragmentRow(msg, pos);
    storageFile.insert(DFSF::TableNameFragments, row);
    moveRows(row, msg.Data.size());
    return true;
}

bool FragmentStorage::removeFragment(DFS::Packets::DeleteSegmentMessage msg) {
    std::string GetStartFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments
        + " WHERE pos = " + std::to_string(msg.Offset) + "ORDER BY pos DESC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DBRow frag = array[0];
        storageFile.deleteRow(DFSF::TableNameFragments, frag);
        std::filesystem::path filePath(msg.Actor + Utils::platformDelimeter() + msg.FileHash);
        return remove(filePath, std::stoull(frag.at("storedPos")), std::stoull(frag.at("size")));
    }
    return false;
}

DFSP::SegmentMessage FragmentStorage::getFragment(uint64_t pos) {
    DFSP::SegmentMessage fragment;

    std::string GetStartFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments
        + " WHERE pos = " + std::to_string(pos) + "ORDER BY pos DESC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetStartFragmentQuery);
    if (!array.empty()) {
        DBRow fragMap = array[0];
        std::filesystem::path filePath(actor.toStdString() + Utils::platformDelimeter() + fileHash);
        fragment.Offset = pos;
        fragment.Data = extract(filePath, pos, std::stoull(fragMap.at("size")));
        fragment.Actor = this->actor.toStdString();
        fragment.FileHash = this->fileHash;
        return fragment;
    }
    return fragment;
}

DBRow FragmentStorage::getPreviousFragment(uint64_t number) {
    DBRow ret;
    std::string GetPrevFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE pos < "
        + std::to_string(number) + "ORDER BY pos DESC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetPrevFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DBRow FragmentStorage::getNextFragment(uint64_t number) {
    DBRow ret;
    std::string GetNextFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE pos > "
        + std::to_string(number) + "ORDER BY pos ASC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetNextFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DBRow FragmentStorage::getRealPreviousFragment(uint64_t number) {
    DBRow ret;
    std::string GetPrevFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE storedPos < "
        + std::to_string(number) + "ORDER BY pos DESC LIMIT 1";
    std::vector<DBRow> array = storageFile.select(GetPrevFragmentQuery);
    if (!array.empty()) {
        ret = array[0];
    }
    return ret;
}

DBRow FragmentStorage::getRealNextFragment(uint64_t number) {
    DBRow ret;
    std::string GetNextFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE storedPos > "
        + std::to_string(number) + "ORDER BY pos ASC LIMIT 1";
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
        + std::to_string(number) + "ORDER BY pos DESC LIMIT 1";
    res = storageFile.select(GetPrevFragmentQuery);
    if (!res.empty()) {
        ret.first = res[0];
    }
    std::string GetNextFragmentQuery = "SELECT * FROM " + DFSF::TableNameFragments + " WHERE pos > "
        + std::to_string(number) + "ORDER BY pos ASC LIMIT 1";
    res = storageFile.select(GetNextFragmentQuery);
    if (!res.empty()) {
        ret.second = res[0];
    }
    return ret;
}

DBRow FragmentStorage::makeFragmentRow(DFS::Packets::SegmentMessage msg, uint64_t storedPos) {
    DBRow row;
    row.insert({ "pos", std::to_string(msg.Offset) });
    row.insert({ "storedPos", std::to_string(storedPos) });
    row.insert({ "size", std::to_string(msg.Data.size()) });
    row.insert({ "fragHash", Utils::calcKeccak(msg.Data) });
    return row;
}

uint64_t FragmentStorage::writeFragment(DFS::Packets::SegmentMessage msg) {
    std::filesystem::path filePath(msg.Actor + Utils::platformDelimeter() + msg.FileHash);
    std::pair<DBRow, DBRow> prevnext = getPrevNextPairFragment(msg.Offset);
    uint64_t posToWrite = 0;
    if (prevnext.first.empty()) {
        posToWrite = 0;
    } else if (prevnext.second.empty()) {
        posToWrite = std::filesystem::file_size(filePath);
    } else {
        posToWrite = std::stoull(prevnext.second["storedPos"]);
    }
    write(filePath, posToWrite, msg.Data);
    return posToWrite;
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

    for (i = pos; i < fz; i = i + DFS::Basic::sectionSize) { // copy old data to new temp file
        if (i + DFS::Basic::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i,
                                                           DFS::Basic::sectionSize);
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

        for (i = 0; i < fzres; i = i + DFS::Basic::sectionSize) { // copy new data to old file
            if (i + DFS::Basic::sectionSize < fzres) {
                boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i,
                                                               DFS::Basic::sectionSize);
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
    for (i = pos + size; i < fz; i = i + DFS::Basic::sectionSize) { // copy old data to new temp file
        if (i + DFS::Basic::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i,
                                                           DFS::Basic::sectionSize);
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

    for (i = 0; i < fzres; i = i + DFS::Basic::sectionSize) { // copy new data to old file
        if (i + DFS::Basic::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i,
                                                           DFS::Basic::sectionSize);
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
