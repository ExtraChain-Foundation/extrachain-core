#ifndef TOKEN_MANAGER_H
#define TOKEN_MANAGER_H

#include <QDebug>
#include <QObject>

#include "datastorage/actor.h"
#include "utils/db_connector.h"

class ExtraChainNode;
class Transaction;

static const uint size_of_data_list = 7;

struct TokenData {
    std::string actor, owner, count, name, ticker, color, smart;

    QJsonDocument toJsonDocument();
    DBRow         toDBRow();
};

class TokenManager : public QObject {
    Q_OBJECT

    ExtraChainNode                               *node;
    QMap<ActorId, QMap<std::string, std::string>> tokenBalance;

    void sendInitialTransaction(
        const std::shared_ptr<Actor<KeyPrivate>> sender,
        ActorId                                  receiver,
        std::string                              quantity);
    // std::shared_ptr<Actor<KeyPrivate>> createPrivateActor();
    void initializeTokenArray();
    bool tokenExist(const std::string &nameToken);
    bool tokentTickerExist(const std::string &tickerToken);

public:
    TokenManager(ExtraChainNode *node);
    ~TokenManager() = default;

    bool isContract(const QString &pathFile);

public slots:
    void createToken(
        const std::string &tokenCount,
        const std::string &tokenName,
        const std::string &symbol,
        const ActorId     &owner,
        const std::string &color);
    void checkIsContract(const QString &pathToFile);

protected:
    bool checkJsonObjectHasTokenFields(const QJsonObject &jsonObj);

signals:
    void verifyActor(Actor<KeyPublic> actor);
    void sendTransactionCreateToken(const ActorId &actorId, const Transaction &tx);
    void saveActorInPrivateProfile(
        const QByteArray &id,
        const QString    &type    = "wallet",
        const bool       &rewrite = false);
    void errorNameTokenExist(const QString &);
    void errorTickerTokenExist(const QString &);
    void added();
    void sendToken(const ActorId &actor, const QString &pathToJson);
};

#endif // TOKEN_MANAGER_H
