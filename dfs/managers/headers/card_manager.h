#ifndef CARD_MANAGER_H
#define CARD_MANAGER_H

#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"
#include <iterator>

class CardManager
{

public:
    static QList<QByteArray> sorting(QList<QByteArray> data);
    static BigNumber getLastSavedFile(const BigNumber &actorId, const based_dfs_struct::Type type);
    static QList<QByteArray> getMyNew();
    static QList<QByteArray> getPosts(const BigNumber &userId);
    static QStringList getAll(based_dfs_struct::Type type);
    static QStringList getForUser(based_dfs_struct::Type type, QString userId);
    static QList<QByteArray> getMyEvents();
    static QList<QByteArray> getEvents(const BigNumber &userId);
    static QList<QByteArray> getAllMyChat();
    static QByteArray getProfileById(const BigNumber &userId);
    static BigNumber getNameForNewFile(based_dfs_struct::Type type);
    static QString getFileByName(const based_dfs_struct::Type type, const QByteArray &name);
    static void appendToCard(based_dfs_struct::Type type, const QByteArray &serialize,
                             const BigNumber &userId);
    static void createdAllCards(const BigNumber &userId);
    static int checkDfsState(const BigNumber &userId);
    static void createdAllConnections();
    static QMap<based_dfs_struct::Type, QByteArray> getCardHashFromRoot(const BigNumber &userId);
    static int createdCardFilesConnection(const BigNumber &userId);

    static QList<QString> getAllFiles(const BigNumber &userId);
    static std::vector<std::pair<std::string, std::string>> getAllFileWithHash(const BigNumber &userId);
    //
    static QList<QByteArray> getUserPosts(BigNumber userdId);
    static QList<QByteArray> getEventsTemp(BigNumber userId);
    // temp for search
    static QStringList existsProfileFiles();
    static QStringList getImagesFromJson(const QByteArray &json);

    static QStringList getAllNotEmptyCardFile(const BigNumber &userId);

    static QStringList getFilesByType(const QByteArray &userId, based_dfs_struct::Type &type);
    static QByteArray getLastFileName(const QByteArray &userId);
    //    function to create
};

#endif // CARD_MANAGER_H
