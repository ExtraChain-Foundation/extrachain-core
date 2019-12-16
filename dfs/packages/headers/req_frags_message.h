#ifndef REQ_FRAGS_MESSAGE_H
#define REQ_FRAGS_MESSAGE_H

#include "dumessage.h"
#include <QFile>
#include <QList>
#include <QByteArray>

namespace DFSMessage {

struct req_frags_message : public DUMessage
{
    const short FIELDS_COUNT = 2;

    QString filePath;
    QByteArray listFrag;

    req_frags_message(const QByteArray &filePath, QByteArray listFrag);
    req_frags_message(const QByteArray &serialized);
    ~req_frags_message() override final;

    bool empty() const;
    const QList<QByteArray> serializedParams() const override final;

public:
    QString getFilePath() const;
    QByteArray getListFrag() const;
};
}

#endif // REQ_FRAGS_MESSAGE_H
