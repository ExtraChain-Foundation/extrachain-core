#ifndef CARD_MANAGER_H
#define CARD_MANAGER_H

#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"
#include <iterator>

class CardManager
{

public:
    static QStringList getAll(based_dfs_struct::Type type);
    static QStringList getForUser(based_dfs_struct::Type type, QString userId);
    static QStringList getFilesByType(const QByteArray &userId, based_dfs_struct::Type &type);
    static QByteArray getLastFileName(const QByteArray &userId);
    static QStringList getAllFiles(const QByteArray &userId);
    static based_dfs_struct::Type getTypeByName(const QString &path, const QByteArray &uxerId);
    //    function to create
};

#endif // CARD_MANAGER_H
