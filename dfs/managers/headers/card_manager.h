#ifndef CARD_MANAGER_H
#define CARD_MANAGER_H

#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"
#include "utils/db_connector.h"

class CardManager
{

public:
    static std::vector<std::string> getAll(DfsStruct::Type type);
    static std::vector<std::string> getFilesByType(const std::string &userId, DfsStruct::Type type);
    static QStringList getAllFiles(const QByteArray &userId);
    static DfsStruct::Type getTypeByName(const QString &fullPath);

    static std::string pathToRoot(std::string userId);
    static std::string buildPathForFile(const std::string &userId, const std::string &file,
                                        DfsStruct::Type type, bool localFormat);
    static std::vector<std::string> buildPathForFiles(const std::string &userId,
                                                      const std::vector<std::string> &files,
                                                      DfsStruct::Type type, bool localFormat);

    static QString cutPath(QString fullPath);

private:
    CardManager();
};

#endif // CARD_MANAGER_H
