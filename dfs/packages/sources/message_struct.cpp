#include "dfs/packages/headers/message_strcut.h"

Message::dfs_message::dfs_message(const QString &filePath, const long long &packageNumber,
                                  const long long &countFilePackage, const QByteArray &data)
{
    this->filePath = filePath;
    this->packageNumber = packageNumber;
    this->countFilePackage = countFilePackage;
    this->data_size = data.size();
    this->data = data;
}
