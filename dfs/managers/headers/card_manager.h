#ifndef CARD_MANAGER_H
#define CARD_MANAGER_H

#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"
#include "headers/utils/db_connector.h"
#define DBNAME ".root"
class CardManager
{

public:
    static QStringList getAll(dfsStruct::Type type);
    static QStringList getFilesByType(const QString &userId, dfsStruct::Type type);
    static QByteArray getLastFileName(const QString &userId);
    static QStringList getAllFiles(const QByteArray &userId);
    static dfsStruct::Type getTypeByName(const QString &path, const QByteArray &userId);

    static QString buildPathForFile(const QString &userId, const QString &file, dfsStruct::Type type,
                                    bool localFormat);
    static QStringList buildPathForFiles(const QString &userId, const QStringList &files,
                                         dfsStruct::Type type, bool localFormat);

private:
    CardManager();
};

#endif // CARD_MANAGER_H
