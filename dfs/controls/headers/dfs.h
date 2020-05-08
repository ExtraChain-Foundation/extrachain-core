#ifndef DFS_H
#define DFS_H

#include "dfs/managers/headers/card_manager.h"
#include "dfs/packages/headers/ui_messages.h"
#include "dfs/packages/headers/dfs_changes.h"
#include "dfs/packages/headers/all.h"
#include "dfs/managers/headers/sender.h"
#include "dfs/managers/headers/dfsnetmanager.h"
#include "utils/utils.h"
#include "utils/db_connector.h"
#include "dfs/controls/headers/subscribe_controller.h"
#include "dfs/types/headers/cardfile.h"
#include <QVector>
#include <QTimer>
#include <QDirIterator>
#include <iterator>
#ifdef ETALONIUM_CLIENT
#include <QImage>
#include <QImageReader>
#endif

class Dfs : public QObject
{

    Q_OBJECT

private:
    // send from nodeManger
    AccountController *accountControler;
    ActorIndex *actorIndex = nullptr;
    DBConnector uCards;
    Sender *sender = nullptr;
    // DFSResolver *resolver;
public slots:
    /*DFS 1.5*/
    void dfsSyncUsers(QList<QString> userID, const SocketPair &receiver = SocketPair());
    void dfsSyncT();
    void dfsSync(const SocketPair &receiver);
    bool dfsValidate(QByteArray userID);
    QList<QByteArray> dfsValidateAll();
    /*DFS 1.5*/
private:
    void initDFS(const QByteArray &userId);
    void saveToDFS(const QString &path, const QByteArray &data,
                   const DfsStruct::Type &type = DfsStruct::Type::Image);
    void saveStaticFile(QString fileName, DfsStruct::Type type, bool needStored);
    void saveFN(const QString tmpPath, const QString &path, const DfsStruct::Type &type);
    bool appendToCard(const QString &path, const QByteArray &userId, const DfsStruct::Type &type,
                      bool isFilePath);
    void cardDiffRequest(const QString &oldCard, const QString &newCard);
    void getDFSStatus();

public:
    DFSNetManager *dfsNetManager = nullptr;
    Dfs(ActorIndex *actorIndex, AccountController *accControler, QObject *parent = nullptr);
    ~Dfs();

public:
    void initDFSNetManager();
    DFSNetManager *getDfsNetManager() const;
    void setDfsNetManager(DFSNetManager *value);
    void fileResponse(const QString filePath, const SocketPair &receiver);
    void sendFragments(QString path, QByteArray frags, SocketPair receiver);
    Sender *getSender() const;

    void responseRequestLast(const DistFileSystem::requestLast &request, SocketPair receiver);
    void responseResponseLast(const DistFileSystem::responseLast &response, SocketPair receiver);
    void responseRequestCardPath(const DistFileSystem::RequestCardPart &request, SocketPair receiver);
    void responseResponseCardPath(const DistFileSystem::ResponseCardPart &response, SocketPair receiver);

    void applyCardFileChange(DistFileSystem::CardFileChange, SocketPair receiver);

    QStringList tmpFiles() const;

signals:
    void finished();
    void sendMsg(const QByteArray &data, const QByteArray &msgType, const SocketPair &receiver);

    void resolveMsg(const QByteArray &msg, int dMsgType, const SocketPair &receiver);
    void sendQ(const QString &filePath, const DfsStruct::Type &type, const SocketPair &receiver);
    void fileAdded(QString path, QString original, DfsStruct::Type type, QByteArray actorId);
    void fileChanged(QString path);
    void fileDuplicated(QString path, QString original, DfsStruct::Type type);
    void sendFromNetwork(int saveType, QString file, QByteArray data, const DfsStruct::Type type);
    void connectToServer();
    void networkCreated();
    void requestFile(const QString &filePath, const SocketPair &receiver = SocketPair());
    void titleReceived(QString filePath);

public slots:
    void initMyLocalStorage();
    void initUser(BigNumber userId);

    void save(int saveType, QString file, QByteArray data, const DfsStruct::Type type);
    void editData(QString userId, QString fileName, DfsStruct::Type type, QByteArray data);
    void editSqlDatabase(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                         QByteArrayList sqlChanges);
    bool applyChanges(DistFileSystem::DfsChanges &dfsChanges);
    void applyReplace(QString userId, QString fileName, QString dfsFileName, DfsStruct::Type type);
    // void appendData(QString userId, QString fileName, QByteArray data);
    void process();
    void startDFS();
    void requestFileHandle(const QString &filePath, const SocketPair &receiver);
    void requestFileUiHandle(QString filePath);
    void titleReceivedHandle(QString filePath);
    void searchTmp();
    void requestCardById(QByteArray userId, const SocketPair &receiver = SocketPair());
    void requestAllCards();

private:
    QByteArray buildDfsPath(QString originalFile, QByteArray hash, QByteArray userID, DfsStruct::Type type);
    bool createStored(QString filePath, const QByteArray &userId, const DfsStruct::Type &type);
    bool appendToStored(DistFileSystem::DfsChanges &dfsChanges, bool init);
    void updateFromNewStored(QString filePath);
    bool applyChangesBytes(const DistFileSystem::DfsChanges &dfsChanges);
    bool applyChangesSql(const DistFileSystem::DfsChanges &dfsChanges);
    DfsStruct::Type getFileType(const QString &filePath);
    bool isHaveStoredType(int type);

    QSet<QString> ignoredIds;
    QTimer *timerTmpFiles;
    QStringList m_tmpFiles;
    QVector<std::pair<qint64, QString>> m_reqFiles;
};

#endif // DFS_H
