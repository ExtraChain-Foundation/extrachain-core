#ifndef CARD_MANAGER_H
#define CARD_MANAGER_H

#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"

class CardManager
{
public:
    static QStringList getAll(based_dfs_struct::Type type);
    static QStringList getFilesByType(const QString &userId, based_dfs_struct::Type type);
    static QByteArray getLastFileName(const QString &userId);
    static QStringList getAllFiles(const QByteArray &userId);
    static based_dfs_struct::Type getTypeByName(const QString &path, const QByteArray &userId);

    static QString buildPathForFile(const QString &userId, const QString &file, based_dfs_struct::Type type,
                                    bool localFormat);
    static QStringList buildPathForFiles(const QString &userId, const QStringList &files,
                                         based_dfs_struct::Type type, bool localFormat);

private:
    CardManager() = default;
};

#endif // CARD_MANAGER_H
