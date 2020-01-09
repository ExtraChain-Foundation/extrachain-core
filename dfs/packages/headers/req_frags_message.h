#ifndef REQ_FRAGS_MESSAGE_H
#define REQ_FRAGS_MESSAGE_H

#include "dfs/packages/headers/dfs_message_interface.h"
#include "headers/network/packages/message_interface.h"

#include <QFile>
#include <QList>
#include <QByteArray>

namespace DistFileSystem {

struct ReqFragsMessage : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 2;

    QString filePath;
    QByteArray listFrag;

    const QList<QByteArray> serializedParams() const;

    // ISmallMessage interface
public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};
}

#endif // REQ_FRAGS_MESSAGE_H
