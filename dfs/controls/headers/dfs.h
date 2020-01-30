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
#include <QTimer>
#include <QDirIterator>
#include <iterator>
#ifdef ETALONIUM_CLIENT
#include <QImage>
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
                   const DfsStruct::Type &type = DfsStruct::Type::images);
    void saveStaticFile(QString fileName, DfsStruct::Type type, bool needStored);
    void saveFN(const QString tmpPath, const QString &path, const DfsStruct::Type &type);
    bool appendToCard(const QString &path, const QByteArray &userId, const DfsStruct::Type &type,
                      bool isFilePath);
    void cardDiffRequest(const QString &oldCard, const QString &newCard);
    void getDFSStatus();

public slots:
    void checkAc(const QByteArray &actorId, const QStringList &request, const SocketPair &receiver);

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

    void responseRequestLast(QByteArray userId, SocketPair receiver);
    void responseResponeLast(QByteArray userId, QByteArray pHash, QByteArray cHash);
    void applyCardFileChange(DistFileSystem::CardFileChange);

    QStringList tmpFiles() const;

signals:
    void finished();
    void sendMsg(const QByteArray &data, const QByteArray &msgType, const SocketPair &receiver);

    void resolveMsg(const QByteArray &msg, int dMsgType, const SocketPair &receiver);
    void sendQ(const QString &filePath, const DfsStruct::Type &type, const SocketPair &receiver);
    void usersChanges(const QByteArray &path, const DfsStruct::Type &type, const QByteArray &actorId);
    void fileChanged(QString path);
    void sendFromNetwork(int saveType, QString file, QByteArray data, const DfsStruct::Type type);
    void connectToServer();
    void networkCreated();
    void newNotify(const notification ntf);

public slots:
    void initMyLocalStorage();
    void initUser(BigNumber userId);

    void save(int saveType, QString file, QByteArray data, const DfsStruct::Type type);
    void editData(QString userId, QString fileName, DfsStruct::Type type, QByteArray data);
    void editSqlDatabase(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                         QByteArrayList sqlChanges);
    bool applyChanges(const DistFileSystem::DfsChanges &dfsChanges);
    // void appendData(QString userId, QString fileName, QByteArray data);
    void process();
    void startDFS();
    void requestFile(const QString &filePath, const SocketPair &receiver = SocketPair());
    void searchTmp();
    void requestCardById(QByteArray userId, const SocketPair &receiver = SocketPair());
    void requestAllCards();

private:
    QByteArray buildDfsPath(QString originalFile, QByteArray hash, QByteArray userID, DfsStruct::Type type);
    bool createStored(QString filePath, const QByteArray &userId, const DfsStruct::Type &type);
    bool appendToStored(QString filePath, QByteArray data, QString range, int type, QString userId, bool init,
                        QByteArray hash);
    void updateFromNewStored(QString filePath);
    bool applyChangesBytes(const DistFileSystem::DfsChanges &dfsChanges);
    bool applyChangesSql(const DistFileSystem::DfsChanges &dfsChanges);
    DfsStruct::Type getFileType(const QString &filePath);

    QTimer *timerTmpFiles;
    QStringList m_tmpFiles;
};

#endif // DFS_H
