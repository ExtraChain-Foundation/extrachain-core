#ifndef DCLOSING_H
#define DCLOSING_H
#include "dumessage.h"
namespace DFSMessage {

struct DClosing : public DUMessage
{

    const short FIELDS_COUNT = 2; // 3;

    QByteArray title_hash = "";
    long long PckgAmoutR = 0;
    //    std::vector<long long> pckgUpset;

    DClosing(const QByteArray &title_hash, const long long &pckAF);
    DClosing(const QByteArray &serialized);
    ~DClosing() override final;
    const QList<QByteArray> serializedParams() const override final;

private:
    //    const QByteArray pckgUpsetSerialize() const;
    //    const std::vector<long long> pckgUpsetDeserialize(const QByteArray &serialized) ;
};
}
#endif // DCLOSING_H
