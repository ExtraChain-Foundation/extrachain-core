#ifndef TITLE_MESSAGE_H
#define TITLE_MESSAGE_H

#include "dfs/packages/headers/dfs_message_interface.h"
#include "headers/network/packages/message_interface.h"

#include <QFile>
namespace DistFileSystem {
/*typedef*/ struct titleMessage : public Messages::ISmallMessage
{
    const short FIELDS_COUNT = 5;

    QString filePath;
    unsigned long pckgsAmount = 0;
    long long fileSize = 0;
    unsigned int f_type;
    QByteArray dataHash; // Keccak256

    void calcHash(); // filesize, pckgAmount, datahash

    const QList<QByteArray> serializedParams() const;
    void operator=(titleMessage title);
    void operator=(const QByteArray &serialized);

    // ISmallMessage interface
public:
    void operator=(QByteArray &serialized) override;

    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
} /*TitleMessage*/;
typedef struct titleMessage TitleMessage;
}

#endif // TITLE_MESSAGE_H
