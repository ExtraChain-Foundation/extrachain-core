#ifndef TOKEN_MANAGER_H
#define TOKEN_MANAGER_H

#include <QDebug>
#include <QObject>

#include "datastorage/actor.h"
#include "utils/db_connector.h"

class ExtraChainNode;
class Transaction;

static const std::string contract_profile = "contract_profile";
static const uint size_of_data_list = 7;

struct TokenData {
    std::string owner, count, name, symbol, color, url;

    QJsonDocument toJsonDocument();
    DBRow toDBRow();
};

class TokenManager : public QObject {
    Q_OBJECT

    ExtraChainNode &node;
    QMap<ActorId, QMap<std::string, std::string>> tokenBalance;

    void sendInitialTransaction(
        const std::shared_ptr<Actor<KeyPrivate>> sender,
        ActorId receiver,
        std::string quantity);
    // std::shared_ptr<Actor<KeyPrivate>> createPrivateActor();
    void initializeTokenArray();
    bool tokenExist(const std::string &nameToken);
    bool tokenSymbolExist(const std::string &symbolToken);

public:
    TokenManager(ExtraChainNode &node, QObject *parent = nullptr);
    ~TokenManager() = default;

    bool isContract(const QString &pathFile);

public slots:
    void createToken(
        const std::string &tokenCount,
        const std::string &tokenName,
        const std::string &symbol,
        const ActorId &rulAddress,
        const std::string &color);
    void checkIsContract(const QString &pathToFile);

protected:
    bool checkJsonObjectHasTokenFields(const QJsonObject &jsonObj);

signals:
    void verifyActor(Actor<KeyPublic> actor);
    void sendTransactionCreateToken(const Transaction &tx);
    void saveActorInPrivateProfile(
        const QByteArray &id,
        const QString &type = "wallet",
        const bool &rewrite = false);
    void errorNameTokenExist(const QString &);
    void errorSymbolTokenExist(const QString &);
    void added();
    void sendToken(const QString &pathToJson);
};

#endif // TOKEN_MANAGER_H
