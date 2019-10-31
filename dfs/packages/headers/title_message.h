#ifndef TITLE_MESSAGE_H
#define TITLE_MESSAGE_H

#include "dumessage.h"
#include <QFile>
namespace Message {

struct title_message : public DUMessage
{
    const short FIELDS_COUNT = 5;

    QString filePath;
    long long pckgsAmount = 0;
    long long fileSize = 0;
    QByteArray f_type;

    QByteArray dataHash; // Keccak256

    title_message(const QString &filePath);
    title_message(const QByteArray &serialized);
    title_message(const QString &filePath, const long long &pckgsAmount, const long long &fileSize,
                  const QByteArray &hash, const QByteArray &f_type);
    ~title_message() override final;

    bool empty() const;
    const QList<QByteArray> serializedParams() const override final;
};
}

#endif // TITLE_MESSAGE_H
