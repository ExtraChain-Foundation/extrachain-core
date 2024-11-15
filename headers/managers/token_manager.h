#ifndef TOKEN_MANAGER_H
#define TOKEN_MANAGER_H

#include <QDebug>
#include <QObject>

#include "blockchain/actor.h"
#include "utils/db_connector.h"

class ExtraChainNode;
class Transaction;

static const uint size_of_data_list = 7;

struct TokenData {
    std::string token, owner, count, name, ticker, color, smart;

    bool operator==(const TokenData& other) const {
        return token == other.token &&
               owner == other.owner &&
               count == other.count &&
               name == other.name &&
               ticker == other.ticker &&
               color == other.color &&
               smart == other.smart;
    }

           // Overload the inequality operator
    bool operator!=(const TokenData& other) const {
        return !(*this == other);
    }

    QJsonDocument toJsonDocument();
    DbRow         toDBRow();
};

enum class CreateTokenError {
    InvalidAmount,
    InvalidName,
    ExistToken
};

class TokenManager : public QObject {
    Q_OBJECT

    ExtraChainNode *node;

    void sendInitialTransaction(const ActorId &owner, const TokenId &token, const BigNumberFloat &amount);
    // std::shared_ptr<Actor<KeyPrivate>> createPrivateActor();
    void initializeTokenArray();
    bool tokenExist(const std::string &nameToken, const std::string &tickerToken);

public:
    TokenManager(ExtraChainNode *node);
    ~TokenManager() = default;

    bool isContract(const QString &pathFile);

    static bool isValidName(const std::string &name);
    static bool isValidTicker(const std::string &ticker);
    static QMap<QString, QString> mapTokens();

public slots:
    std::expected<TokenData, CreateTokenError> createToken(
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
    void added(const ActorId &owner, const TokenId &token);
    void sendToken(const ActorId &actor, const QString &pathToJson);
    void newToken();
};

#endif // TOKEN_MANAGER_H
