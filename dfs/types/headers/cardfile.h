#include <QString>
#include <QFile>

#include <optional>

#include "dfs/types/headers/dfstruct.h"
#include "utils/db_connector.h"
#include "dfs/managers/headers/card_manager.h"

class CardFile
{
public:
    CardFile(QString userId);

    QString userId() const;
    QString fileName() const;
    bool isExists();
    bool open();
    bool close();

    std::optional<DBRow> last();

    bool append(QString fileId, int type, int version, QByteArray sign, bool isFilePath = false, int key = 1);
    bool updateLastCache();
    std::vector<DBRow> select(int count, int offset);

private:
    bool updateNextId();

    QString m_userId;
    QString m_fileName;
    QString m_lastCacheName;
    DBConnector m_db;
};
