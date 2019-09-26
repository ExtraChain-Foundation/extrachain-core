#ifndef DFS_UNIVERSAL_H
#define DFS_UNIVERSAL_H
#include <QObject>
#include "network/packages/base_message.h"

namespace Messages {
static const QByteArray DFS_CHANGES_MESSAGE = "dfsChanges";
class DfsMessage : public BaseMessage
{
    Q_OBJECT

    QByteArray data;
    int size;
    QString filePath;
    int packageNumber;
    int countFilePackage;

    short getFieldsCount() const override;
    void initFields(QList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;

public:
    DfsMessage()
        : BaseMessage()
    {
    }

    const QByteArray hash() const override final;
    DfsMessage(const QByteArray &data, int size, const QString &filePath, int packageNumber,
               int countFilePackage);
    DfsMessage(const QByteArray &serialize);
    DfsMessage(const DfsMessage &temp);
    DfsMessage(QList<QByteArray> &list);
    ~DfsMessage() override;
    DfsMessage operator=(const DfsMessage &temp);
    QByteArray serialize() const override final;
    void deserialize(const QByteArray &serialized) override;
    QByteArray getData() const;
    QString getFilePath() const;

    //    QList<QByteArray> crasherDfsMessagesSerialize(const QByteArray &data, int size,
    //                                                  const QString &filePath);
    int getSize() const;
    void setSize(int value);
    int getPackageNumber() const;
    void setPackageNumber(int value);
    int getNeedsByteCount() const;
    void setNeedsByteCount(int value);
};
}
#endif // DFS_UNIVERSAL_H
