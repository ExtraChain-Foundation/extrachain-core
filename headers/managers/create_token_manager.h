#pragma once

#include <QDebug>
#include <QObject>

#include "datastorage/actor.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/transaction.h"
#include "enc/key_private.h"
#include "utils/bignumber_float.h"

static const std::string folder_tokens = "tokens";
static const std::string contract_profile = "contract_profile";
static const uint size_of_data_list = 7;

class CreateTokenManager : public QObject {
  Q_OBJECT

  ActorIndex *actorIndex;
  QMap<QByteArray, QMap<QByteArray, QByteArray>> tokenBalance;

  bool savePrivateActor(Actor<KeyPrivate> actor);
  void sendInitialTransaction(const std::shared_ptr<Actor<KeyPrivate> > sender, ActorId receiver,
                                            QByteArray quantity);
  std::shared_ptr<Actor<KeyPrivate> > createContract(QByteArray tokenName);
  void initializeTokenArray();

public:
  CreateTokenManager(ActorIndex *actorIndex, QObject *parent = nullptr);
  ~CreateTokenManager() = default;

public slots:
  void createToken(QByteArray tokenCount, QByteArray tokenName,
                             QByteArray relAddress, QByteArray color);

signals:
  void verifyActor(Actor<KeyPublic> actor);
  void sendTransactionCreateToken(const Transaction& tx);
  void saveActorInPrivateProfile(const QByteArray &id,
                                 const QString &type = "wallet",
                                 const bool &rewrite = false);
};
