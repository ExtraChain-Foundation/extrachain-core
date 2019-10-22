#ifndef TITLE_MESSAGE_H
#define TITLE_MESSAGE_H

#include "dfs_message_interface.h"
#include <QFile>
namespace Message {

struct title_message : public IDfs_Message
{
    const short FIELDS_COUNT = 4;
    const int m_type = dfsMessageType::titleMessage;

    QString filePath;
    long long pckgsAmount = 0;
    long long fileSize = 0;

    QByteArray dataHash; // Keccak256

    title_message(const QString &filePath);
    title_message(const QByteArray &serialized);
    title_message(const QString &filePath, const long long &pckgsAmount, const long long &fileSize,
                  const QByteArray &hash);
    ~title_message() override final;
    const QList<QByteArray> serializedParams() const override;
};
}

#endif // TITLE_MESSAGE_H
