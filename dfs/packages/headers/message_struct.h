#ifndef MESSAGE_STRCUT_H
#define MESSAGE_STRCUT_H

#include "dfs/types/headers/dfstruct.h"
#include "dfs_message_interface.h"
namespace Message {
struct dfs_message : public IDfs_Message
{

    const int m_type = dfsMessageType::fileDataMessage;
    const short FIELDS_COUNT = 3;

    QByteArray title_hash;
    long long pckgNumber;
    QByteArray data;

    dfs_message(const QByteArray &hash, const long long &pckgNumber, const QByteArray &data);
    dfs_message(const QByteArray &serialized);
    dfs_message(const dfs_message &temp);
    ~dfs_message() override final;

    const QList<QByteArray> serializedParams() const override;
};

struct file_data_message
{
};
}
#endif // MESSAGE_STRCUT_H
