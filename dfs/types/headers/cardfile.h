#include <QString>
#include <QFile>

#include "dfs/types/headers/dfstruct.h"
#include "utils/db_connector.h"

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

    bool append(QString fileId, int type, QByteArray sign, bool isFilePath = false);

private:
    bool updateNextId();

    QString m_userId;
    QString m_fileName;
    DBConnector m_db;
};
