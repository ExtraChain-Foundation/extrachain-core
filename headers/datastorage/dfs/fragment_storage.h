#ifndef FRAGMENT_STORAGE_H
#define FRAGMENT_STORAGE_H

#include "dfs_controller.h"
#include "extrachain_global.h"
#include "managers/extrachain_node.h"
#include "utils/db_connector.h"
#include "utils/dfs_utils.h"
#include <QThread>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

class FragmentWriter;
class EXTRACHAIN_EXPORT FragmentStorage {
private:
    DBConnector storageFile;
    ActorId actor;
    std::string fileName;
    std::string fileHash;

public:
    FragmentStorage(ActorId Actor, std::string FileName, std::string FileHash);
    FragmentStorage(DFSP::SegmentMessage segmentMessage);
    ~FragmentStorage() = default;

    bool initLocalFile(uint64_t filesize);
    bool initHistoricalChain();
    bool insertFragment(DFSP::SegmentMessage msg);
    bool editFragment(DFSP::EditSegmentMessage msg);
    bool removeFragment(DFSP::DeleteSegmentMessage msg);
    DFSP::SegmentMessage getFragment(uint64_t pos);
    bool applyChanges(const std::string& data, uint64_t pos);

private:
    DBRow getPreviousFragment(uint64_t number);
    DBRow getNextFragment(uint64_t number);
    DBRow getRealPreviousFragment(uint64_t number);
    DBRow getRealNextFragment(uint64_t number);
    std::pair<DBRow, DBRow> getPrevNextPairFragment(uint64_t number);
    DBRow makeFragmentRow(DFSP::SegmentMessage msg, uint64_t storedPos);
    DBRow makeFragmentRow(uint64_t pos, uint64_t storedPos, uint64_t size);
    uint64_t writeFragment(DFSP::SegmentMessage msg);
    void moveRows(DBRow curRow, uint64_t moveSize);
    uint64_t write(std::filesystem::path filePath, uint64_t pos, std::string data);
    std::string extract(std::filesystem::path filePath, uint64_t pos, uint64_t size);
    uint64_t remove(std::filesystem::path filePath, uint64_t pos, uint64_t size);
    bool checkRenameFile(const DFS::Packets::EditSegmentMessage& msg);
};

class FragmentWriter : public QThread {
    Q_OBJECT
    DFSP::SegmentMessage m_msg;
    std::vector<std::string> m_compliteFiles;

public:
    FragmentWriter(const DFSP::SegmentMessage& msg, std::vector<std::string> m_compliteFiles,
                   QObject* parent = nullptr);
    ~FragmentWriter() {
        quit();
    }

protected:
    void run() override;

signals:
    void downloadedFile(std::string& actor, std::string& fileName);
    void eraseFromFiles(DFSP::SegmentMessage m_msg);
    void sendFile(std::string& actor, std::string& fileName);
    void downloadProgress(std::string& actor, std::string& fileName, double progress);
    void requestFile(std::string& actor, std::string& fileName);
    void compliteFile(std::string& fileName);
};

#endif // FRAGMENT_STORAGE_H
