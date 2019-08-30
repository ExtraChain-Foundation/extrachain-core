#ifndef CONTRACT_H
#define CONTRACT_H
#include "utils/bignumber.h"
#include "datastorage/transaction.h"
#include "datastorage/actor.h"
#include "datastorage/block.h"
#include <QObject>
#include <QDate>
class Token : public QObject
{
    Q_OBJECT
private:
    BigNumber actorId;
    BigNumber amount_token;
    BigNumber amout_in_work;

    QByteArray Activity;
    QByteArray Funds;
    QByteArray Sum;

    BigNumber price;
    QByteArray logo;
    BigNumber WorkDone;
    BigNumber DaysGone;
    BigNumber Pt;
    BigNumber rating;
    // QByteArray

public:
    BigNumber calcRating();
    BigNumber getWorkDone();
    BigNumber getDaysGone();
    BigNumber getPt();
    BigNumber getSum();
    BigNumber getAmount(QList<Block>, BigNumber userId);
    Token(QObject *parent = nullptr);
    Token(BigNumber actorId, QObject *parent = nullptr);
    BigNumber getPrice() const;

    BigNumber calcActivity();
    BigNumber calcFunds();
    BigNumber calcSum();
    BigNumber getActorId() const;

signals:
    QByteArray LogoDFS(BigNumber actorId);
public slots:
private:
    QList<QByteArray> getPathtoAllBlocks();
};

class Contract : public QObject
{
    Q_OBJECT
private:
    BigNumber customer;
    BigNumber performer;

    QByteArray location;
    QByteArray event;
    QPair<long long, long long> event_date;
    QList<QByteArray> scope_of_work;
    QByteArray agreement;
    BigNumber amount;

    QByteArray customer_sign;
    QByteArray performer_sign;
    QByteArray first_transaction_hash;
    QByteArray final_transaction_hash;

    bool approve_complete_customer;
    bool approve_complete_performer;

    bool isCompleted;

public:
    Contract(QObject *parent = nullptr);
    Contract(const QByteArray serialize_contract, QObject *parent = nullptr);
    Contract(const Contract &contract, QObject *parent = nullptr);
    Contract(const BigNumber _customer, const BigNumber _performer, const QByteArray _location,
             const QByteArray event, const QPair<long long, long long> _event_date,
             const QList<QByteArray> _scope_of_work, const QByteArray _agreement,
             const BigNumber _amount, QObject *parent = nullptr);

    void operator=(const Contract &contract);
    bool operator==(const Contract &contract) const;

    QByteArray serialize() const;

    QByteArray getSignData() const;

    void signByCustomer(const Actor<KeyPrivate> &actor);
    void signByPerformer(const Actor<KeyPrivate> &actor);

    QByteArray getCustomer_sign() const;
    void setCustomer_sign(const QByteArray &value);
    QByteArray getPerformer_sign() const;
    void setPerformer_sign(const QByteArray &value);

    BigNumber getCustomer() const;
    void setCustomer(const BigNumber &value);

    BigNumber getPerformer() const;
    void setPerformer(const BigNumber &value);

    bool checkCustomerSign(const Actor<KeyPublic> &actor);
    bool checkPerformerSign(const Actor<KeyPublic> &actor);
    //    bool checkCustomerComplete();
    //    bool

    QByteArray getLocation() const;
    void setLocation(const QByteArray &value);

    QByteArray getEvent() const;
    void setEvent(const QByteArray &value);

    QPair<long long, long long> getEvent_date() const;
    void setEvent_date(const QPair<long long, long long> &value);

    QList<QByteArray> getScope_of_work() const;
    void setScope_of_work(const QList<QByteArray> &value);

    QByteArray getAgreement() const;
    void setAgreement(const QByteArray &value);

    BigNumber getAmount() const;
    void setAmount(const BigNumber &value);

    QByteArray getHash() const;
    QByteArray getFileName() const;

    bool makeFirstTransction() const;
    bool makeFinalTransaction() const;

    QByteArray getFirst_transaction_hash() const;
    void setFirst_transaction_hash(const QByteArray &value);

    QByteArray getFinal_transaction_hash() const;
    void setFinal_transaction_hash(const QByteArray &value);

    bool getApprove_complete_performer() const;
    void setApprove_complete_performer(bool value);

    void completeContractByCustomer();
    void completeContractByPerformer();

    bool getIsCompleted() const;
    void setIsCompleted(bool value);

private:
    QByteArray calcDigSig(const Actor<KeyPrivate> &actor);
    bool verifyDigSig(const Actor<KeyPublic> &actor, const QByteArray &data,
                      const QByteArray &digSig);
};

#endif // CONTRACT_H
