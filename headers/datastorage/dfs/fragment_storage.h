#ifndef FRAGMENT_STORAGE_H
#define FRAGMENT_STORAGE_H

#include "extrachain_global.h"
#include "utils/db_connector.h"
#include "utils/dfs_utils.h"
#include <QThread>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

class FragmentWriter;
class EXTRACHAIN_EXPORT FragmentStorage {
private:
    DBConnector storageFile;
    ActorId     actorId;
    std::string fileId;
    std::string hash;

public:
    FragmentStorage(ActorId Actor, std::string FileName, std::string FileHash);
    FragmentStorage(DfsP::SegmentMessage segmentMessage);
    ~FragmentStorage() = default;

    bool                 initLocalFile(std::uint64_t filesize);
    bool                 initHistoricalChain();
    bool                 insertFragment(DfsP::SegmentMessage msg);
    bool                 editFragment(DfsP::EditSegmentMessage msg);
    bool                 removeFragment(DfsP::DeleteSegmentMessage msg);
    DfsP::SegmentMessage getFragment(std::uint64_t pos);
    DfsP::SegmentMessage getFragment(std::string fragHash);
    bool                 applyChanges(const std::string& data, std::uint64_t pos);

private:
    DBRow                   getPreviousFragment(std::uint64_t number);
    DBRow                   getNextFragment(std::uint64_t number);
    DBRow                   getRealPreviousFragment(std::uint64_t number);
    DBRow                   getRealNextFragment(std::uint64_t number);
    std::pair<DBRow, DBRow> getPrevNextPairFragment(std::uint64_t number);
    DBRow                   makeFragmentRow(DfsP::SegmentMessage msg, std::uint64_t storedPos);
    DBRow                   makeFragmentRow(std::uint64_t pos, std::uint64_t storedPos, std::size_t size);
    std::uint64_t                writeFragment(DfsP::SegmentMessage msg);
    void                    moveRows(DBRow curRow, std::uint64_t moveSize);
    std::uint64_t                write(std::filesystem::path filePath, std::uint64_t pos, std::string data);
    std::string             extract(std::filesystem::path filePath, std::uint64_t pos, std::size_t size);
    std::uint64_t                remove(std::filesystem::path filePath, std::uint64_t pos, std::size_t size);
    bool                    checkRenameFile(const Dfs::Packets::EditSegmentMessage& msg);
};

class FragmentWriter : public QThread {
    Q_OBJECT
    DfsP::SegmentMessage     m_msg;
    std::vector<std::string> m_compliteFiles;

public:
    FragmentWriter(
        const DfsP::SegmentMessage& msg,
        std::vector<std::string>    m_compliteFiles,
        QObject*                    parent = nullptr);
    ~FragmentWriter() {
        quit();
    }

protected:
    void run() override;

signals:
    void downloadedFile(Dfs::DirRow dirRow);
    void eraseFromFiles(const DfsP::SegmentMessage m_msg);
    void sendFile(const ActorId& actor, const std::string& fileName, const std::string& messageId = "");
    void downloadProgress(const ActorId& actor, const std::string& fileName, const double progress);
    void requestFile(const ActorId& actor, const std::string& fileName);
    void compliteFile(const std::string& fileName);
    void requestNextFragment(const DfsP::RequestFileSegmentMessage&);
};

#endif // FRAGMENT_STORAGE_H
