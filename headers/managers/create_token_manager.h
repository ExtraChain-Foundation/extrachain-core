#pragma once

#include <QDebug>
#include <QObject>

#include "datastorage/actor.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/transaction.h"
#include "enc/key_private.h"
#include "utils/bignumber_float.h"

static const std::string contract_profile = "contract_profile";
static const uint size_of_data_list = 7;

class CreateTokenManager : public QObject {
  Q_OBJECT

  ActorIndex *actorIndex;
  QMap<std::string, QMap<std::string, std::string>> tokenBalance;

  bool savePrivateActor(Actor<KeyPrivate> actor);
  void sendInitialTransaction(const std::shared_ptr<Actor<KeyPrivate>> sender,
                                                            ActorId receiver, std::string quantity);
  std::shared_ptr<Actor<KeyPrivate>> createContract();
  void initializeTokenArray();
  bool tokenExist(const std::string &nameToken);
  bool tokenSymbolExist(const std::string &symbolToken);

public:
  CreateTokenManager(ActorIndex *actorIndex, QObject *parent = nullptr);
  ~CreateTokenManager() = default;

public slots:
  void createToken(const std::string& tokenCount, const std::string& tokenName, const std::string& symbol,
                   const std::string& relAddress, const std::string& color);

signals:
  void verifyActor(Actor<KeyPublic> actor);
  void sendTransactionCreateToken(const Transaction &tx);
  void saveActorInPrivateProfile(const QByteArray &id,
                                 const QString &type = "wallet",
                                 const bool &rewrite = false);
  void errorNameTokenExist(const QString&);
  void errorSymbolTokenExist(const QString&);
  void added();
};
