/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "managers/extrachain_node.h"

#include "resolve/resolve_manager.h"

ExtraChainNode::ExtraChainNode()
{
    if (sodium_init() != 0)
    {
        qDebug() << "Encryption init error";
        QCoreApplication::exit(-1);
    }
    prepareFolders();
    if (!QFile(".settings").exists())
        createNetManagerIdentificator();
    if (!QFile(".dsettings").exists())
        dfscreateNetManagerIdentificator();
    actorIndex = new ActorIndex();
    prProfile = new PrivateProfile();
    smContractController = new SmartContractManager(actorIndex);
    accController = new AccountController(actorIndex);
    netManager = new NetManager(accController, actorIndex);
    subscribeController = new SubscribeController();
    subscribeController->setExtraChainNode(this);
    actorIndex->setAccController(accController);
    ThreadPool::addThread(netManager);
    //    this->thread()->sleep(1);
    blockchain = new Blockchain(accController, fileMode);
    accController->setBlockchain(blockchain);
    txManager = new TransactionManager(accController, blockchain, this);
    prProfile->setAccountController(accController);
    chatManager = new ChatManager(accController, actorIndex);
    chatManager->setNetManager(netManager);
    //    contractManager = new ContractManager(accController, blockchain);
    dfs = new Dfs(actorIndex, accController);

#ifdef ECLIENT
    notificationManager = new NotificationManager();
    ThreadPool::addThread(notificationManager);
#endif
    resolveManager = new ResolveManager(actorIndex, blockchain, netManager, txManager, accController);
    resolveManager->setNode(this);
    resolveManager->setChatManager(chatManager);
    blockchain->setTxManager(txManager);
    netManager->setResolveManager(resolveManager);
    //    dfs->initDFSNetManager(resolveManager);
    prProfile->setDfs(dfs);
    actorIndex->setResolveManager(resolveManager);
    connectSignals();

    static QTimer getAllActorsTimer;
    connect(&getAllActorsTimer, &QTimer::timeout, this, &ExtraChainNode::getAllActorsTimerCall);
    getAllActorsTimer.start(30000);

    ThreadPool::addThread(blockchain);
    ThreadPool::addThread(actorIndex);
    ThreadPool::addThread(txManager);
    // ThreadPool::addThread(contractManager);
    ThreadPool::addThread(dfs);
    ThreadPool::addThread(smContractController);
    ThreadPool::addThread(resolveManager);
    ThreadPool::addThread(prProfile);
    ThreadPool::addThread(chatManager);

    // FileUpdaterManager fl;
    // fl.verifyMyFiles("02c9b394cf3785389f82");
}

void ExtraChainNode::createCompanyActor(const QString &email, const QString &password)
{
#ifdef ECONSOLE
    // accController->loadActors("-1");
    Actor<KeyPrivate> company;
    QByteArray consoleHash = Utils::calcKeccak(email.toUtf8() + password.toUtf8());

    bool created = false;
    if (QDir("keystore/profile").isEmpty())
    {
        company = CreateCompany(consoleHash);
        emit savePrivateProfile(consoleHash, company.id().toActorId());
        created = true;
    }
    else
    {
        // company = *accController->getAccounts()[0];
        emit loadProfileForConsoleLogin(email.toLatin1(), password.toLatin1());
    }

    if (blockchain->getRecords() <= 0)
    {
        QByteArray td = company.key()->sign("test");
        std::cout << company.key()->verify("test", td) << std::endl;
        TMP::companyActorId = new QByteArray(company.id().toActorId());
        actorIndex->setCompanyId(new QByteArray(company.id().toActorId()));

        QMap<BigNumber, BigNumber> tm;
        tm.insert(0, 0);
        GenesisBlock tmp = blockchain->createGenesisBlock(company, tm);
        blockchain->addBlock(tmp, true);

        // TODO: as console argument
        if (created)
        {
            emit generateSmartContract("1000", "Default Coin", company.id().toActorId(), "#fa4868");

            QString companyId = *TMP::companyActorId;
            DBConnector dbc(
                (DfsStruct::ROOT_FOOLDER_NAME + "/" + companyId + "/" + DfsStruct::ACTOR_CARD_FILE)
                    .toStdString());
            dbc.createTable(Config::DataStorage::cardTableCreation);
            dbc.createTable(Config::DataStorage::cardDeletedTableCreation);
            QString usernamesPath =
                QString(DfsStruct::ROOT_FOOLDER_NAME + "/%1/services/usernames").arg(companyId);
            DBConnector usernamesDB(usernamesPath.toStdString());
            usernamesDB.createTable(Config::DataStorage::userNameTableCreation);
            dfs->save(DfsStruct::DfsSave::Static, "usernames", "", DfsStruct::Type::Service);
        }
    }
#else
    Q_UNUSED(email)
    Q_UNUSED(password)
#endif
}

void ExtraChainNode::initConsoleToken(Transaction tx)
{
    Q_UNUSED(tx)
#ifdef ECONSOLE
    QByteArray data = Serialization::serialize({ tx.serialize() }, Serialization::TRANSACTION_FIELD_SIZE);
    Block lastBlock = blockchain->getLastBlock();
    Block block(data, lastBlock);
    blockchain->signBlock(block);
    qDebug() << "Created block:" << block.getIndex();
    blockchain->addBlock(block);
#endif
}

Actor<KeyPrivate> ExtraChainNode::CreateCompany(QByteArray consoleHash)
{
    accController->createActor(ActorType::Company, consoleHash);

    return *accController->getMainActor();
}

void ExtraChainNode::showMessage(QString from, QString message)
{
    qDebug() << from << " " << message;
}

void ExtraChainNode::connectResolveManager()
{
    //    connect(netManager, &NetManager::MsgReceived, resolveManager, &ResolveManager::resolveMessage);
    //    connect(resolveManager, &ResolveManager::coinRequest, this, &ExtraChainNode::coinResponse);
    //    connect(dfs->getDfsNetManager(), &DFSNetManager::newMessage, resolveManager,
    //            &ResolveManager::resolveMessage);
    // TODO: move
    //    connect(resolveManager, &ResolveManager::sendMsg, netManager, &NetManager::sendMessage);

    connect(this, &ExtraChainNode::sendMsg, resolveManager, &ResolveManager::registrateMsg);
    connect(txManager, &TransactionManager::SendBlock, resolveManager, &ResolveManager::registrateMsg);
    connect(blockchain, &Blockchain::sendMessage, resolveManager, &ResolveManager::registrateMsg);
    //    connect(dfs, &Dfs::newSender, resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::connectSmContractManager()
{
    //    connect(smContractController, &SmartContractManager::verifyActor, netManager,
    //    &NetManager::NewActor); TODO!!!
    //    connect(smContractController, &SmartContractManager::addContractActorInActorIndex, this,
    //            &ExtraChainNode::addActorInActorIndex);
    connect(smContractController, &SmartContractManager::saveActorInPrivateProfile,
            [this](const QByteArray &id, const QString &type, const bool &rewrite) {
                emit nodeEditPrivateProfile({ getHashLoginPrivateProfile(), getIdPrivateProfile() }, type, id,
                                            rewrite);
            });

    //[this](QString userId, Profile profile) { emit profileToUi(userId, profile); });
    connect(this, &ExtraChainNode::nodeEditPrivateProfile, prProfile, &PrivateProfile::editPrivateProfile);

    connect(this, &ExtraChainNode::generateSmartContract, smContractController,
            &SmartContractManager::createContractProfile);
    connect(smContractController, &SmartContractManager::sendTransactionCreateContract, resolveManager,
            &ResolveManager::registrateMsg);
    connect(smContractController, &SmartContractManager::initConsoleToken, this,
            &ExtraChainNode::initConsoleToken);

    // connect(smContractController, &SmartContractManager::sendCurrentToken,netManager,
    // &NetManager::NewActor);
}

void ExtraChainNode::connectTxManager()
{
    // TODOD delete later (s)
    connect(this, &ExtraChainNode::NewTx, txManager, &TransactionManager::addTransaction);
}

ExtraChainNode::~ExtraChainNode()
{
    // netManager->quit();
    // delete netManager;
    delete txManager;
    // delete blockchain;
    delete accController;
    // delete actorIndex;
}

// DFSIndex *ExtraChainNode::getDFSIndex(){
//    return dfsIndex;
//}

Blockchain *ExtraChainNode::getBlockchain()
{
    return blockchain;
}

NetManager *ExtraChainNode::getNetManager()
{
    return netManager;
}

#ifdef ECLIENT
void ExtraChainNode::setNotificationClient(NotificationClient *newNtfCl)
{
    notificationManager->setNotifyClient(newNtfCl);
    notificationManager->setActorIndex(actorIndex);
    notificationManager->setAccController(accController);
}
#endif

Transaction ExtraChainNode::createTransaction(Transaction tx)
{
    if (tx.isEmpty())
    {
        qDebug() << QString("Warning: can not create tx:[%1]. Transaction is empty").arg(tx.toString());
        return Transaction();
    }

    Actor<KeyPrivate> actor = accController->getCurrentActor();
    if (!actor.empty())
    {
        qDebug() << QString("Attempting to create tx:[%1] from user [%2]")
                        .arg(tx.toString(), QString(actor.id().toByteArray()));

        // 1) set prev block id
        BigNumber lastBlockId = blockchain->getLastBlock().getIndex();
        if (lastBlockId.isEmpty())
        {
            qDebug() << QString("Warning: can not create tx:[%1]. There no last block in "
                                "blockchain")
                            .arg(tx.toString());
            return Transaction();
        }
        tx.setPrevBlock(lastBlockId);

        // 2) sign transaction

        tx.sign(actor);
        qDebug() << "send tx" << Transaction::amountToVisible(tx.getAmount()) << "to" << tx.getReceiver();

        // send without fee
        if (tx.getSender() == BigNumber(Trash::NullActor)
            || tx.getSender() == BigNumber(*actorIndex->companyId)
            || tx.getReceiver() == BigNumber(Trash::NullActor)
            || tx.getReceiver() == BigNumber(*actorIndex->companyId))
            emit NewTx(tx);
        else if (tx.getData() == Fee::FREEZE_TX || tx.getData() == Fee::UNFREEZE_TX)
        {
            emit sendMsg(tx.serialize(), Messages::ChainMessage::txMessage);
        }
        else
        {
            BigNumber amountTemp(tx.getAmount());
            if (blockchain->getUserBalance(tx.getSender(), tx.getToken()) - amountTemp - amountTemp / 100
                >= 0)
            {
                // send with fee

                Transaction txFee = tx;
                // restructure tx for fee
                {

                    amountTemp /= 100;
                    txFee.setAmount(amountTemp);
                    txFee.setReceiver(actor.id()); // send fee to my freezeFee
                    // ENUM | Tx hash that fee refer
                    txFee.setData(Serialization::serialize({ tx.getHash(), Fee::FEE_FREEZE_TX }));
                    txFee.sign(actor);
                }

                // send fee tx
                emit sendMsg(txFee.serialize(), Messages::ChainMessage::txMessage); // send fee
                emit sendMsg(tx.serialize(), Messages::ChainMessage::txMessage);
            }
            else
            {
                qDebug() << "Not enough money ";
                return Transaction();
            }
        }

        return tx;
    }
    else

        qDebug() << QString("Warning: can not create tx:[%1]. There no current user").arg(tx.toString());

    return Transaction();
}

Transaction ExtraChainNode::createTransaction(ActorId receiver, BigNumber amount, ActorId token)
{
    if (receiver.isEmpty() || amount.isEmpty())
    {
        qDebug() << QString("Warning: can not create tx without receiver or amount");
        return Transaction();
    }

    Actor<KeyPrivate> actor = accController->getCurrentActor();
    if (!actor.empty())
    {
        qDebug() << actor.id();
        Transaction tx(actor.id(), receiver, amount);
        // add sent tx balances

        tx.setToken(token);
        //        if (actorIndex->companyId != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->companyId))
        //                tx.setSenderBalance(BigNumber(0));

        return this->createTransaction(tx);
    }
    qDebug() << QString("Warning: can not create tx to [%1]. There no current user")
                    .arg(QString(receiver.toByteArray()));
    return Transaction();
}

Transaction ExtraChainNode::createFreezeTransaction(ActorId receiver, BigNumber amount, bool toFreeze,
                                                    ActorId token)
{

    Actor<KeyPrivate> actor = accController->getCurrentActor();

    if (!actor.empty())
    {
        if (receiver.isEmpty())
        {
            qDebug() << "Create freeze tx to me";
            receiver = actor.id();
        }
        else
            qDebug() << "Create freeze tx to" << receiver;

        Transaction tx(actor.id(), receiver, amount);
        // add sent tx balances
        tx.setData(toFreeze ? Fee::FREEZE_TX : Fee::UNFREEZE_TX);
        tx.setToken(token);
        //        if (actorIndex->companyId != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->companyId))
        //                tx.setSenderBalance(BigNumber(0));

        return this->createTransaction(tx);
    }
    qDebug() << QString("Warning: can not create tx to [%1]. There no current user")
                    .arg(QString(receiver.toByteArray()));
    return Transaction();
}

Transaction ExtraChainNode::createTransactionFrom(ActorId sender, ActorId receiver, BigNumber amount,
                                                  ActorId token)
{
    if (receiver.isEmpty() || amount.isEmpty())
    {
        qDebug() << QString("Warning: can not create tx without receiver or amount");
        return Transaction();
    }

    Actor<KeyPrivate> actor = accController->getActor(sender);
    if (!actor.empty())
    {
        qDebug() << actor.id();
        Transaction tx(actor.id(), receiver, amount);
        // add sent tx balances

        tx.setToken(token);
        // tx.setHop(2);
        //        if (actorIndex->companyId != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->companyId))
        //                tx.setSenderBalance(BigNumber(0));
        return this->createTransaction(tx);
    }
    else
    {
        qDebug() << QString("Warning: can not create tx to [%1]. There no current user")
                        .arg(QString(receiver.toByteArray()));
    }
    return Transaction();
}

void ExtraChainNode::getAllActors()
{
    //    QByteArray res = getIdPrivateProfile();
    //    if (!res.isEmpty())
    //        emit getAllActorsNode(res, true);
}
void ExtraChainNode::getAllActorsTimerCall()
{
#ifdef ECLIENT
    QByteArray res = getIdPrivateProfile();
    if (!res.isEmpty())
        emit getAllActorsNode(res, true);
#endif
#ifdef ECONSOLE
    if (accController->getAccountCount() > 0)
    {
        QByteArray res2 = accController->getMainActor()->id().toActorId();

        if (!res2.isEmpty())
        {
            emit getAllActorsNode(res2, true);
        }
    }
#endif
}

void ExtraChainNode::createNetManagerIdentificator()
{
    QFile file(".settings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(BigNumber::random(64).toByteArray());
    file.flush();
    file.close();
}
void ExtraChainNode::dfscreateNetManagerIdentificator()
{
    QFile file(".dsettings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(BigNumber::random(64).toByteArray());
    file.flush();
    file.close();
}

#ifdef ECLIENT
void ExtraChainNode::notificationToken(QString os, QString actorId, QString token)
{
    if (os.isEmpty() || actorId.isEmpty() || token.isEmpty())
        return;
    auto companyId = actorIndex->companyId;
    if (companyId == nullptr)
        return;
    auto company = actorIndex->getActor(*companyId);
    if (company.empty())
        return;
    auto key = company.key();

    QMap<QString, QByteArray> map = {
        { "actor", actorId.toLatin1() },
        { "token", key->encrypt(token.toLatin1(), accController->getMainActor()->key()->getSecKey()) },
        { "os", key->encrypt(os.toLatin1(), accController->getMainActor()->key()->getSecKey()) }
    };

    emit sendMsg(Serialization::serializeMap(map), Messages::GeneralRequest::Notification);
}
#endif

#ifdef ECONSOLE
void ExtraChainNode::connectConsole()
{
    connect(this, &ExtraChainNode::savePrivateProfile, prProfile, &PrivateProfile::savePrivateProfile);
    connect(this, &ExtraChainNode::loadProfileForConsoleLogin, prProfile,
            &PrivateProfile::loadPrivateProfile);
}
#endif

void ExtraChainNode::connectContractManager()
{
}

void ExtraChainNode::connectActorIndex()
{
    connect(actorIndex, &ActorIndex::sendMessage, resolveManager, &ResolveManager::registrateMsg);
    // this connect with service message

    connect(prProfile, &PrivateProfile::setIdProfile, this, &ExtraChainNode::setIdPrivateProfile);
    connect(prProfile, &PrivateProfile::setHashProfile, this, &ExtraChainNode::setHashLoginPrivateProfile);
}

void ExtraChainNode::dfsConnection()
{
    // init dfs for user
    // connect(this, &ExtraChainNode::ready, netManager, &NetManager::startNetwork);
    connect(this, &ExtraChainNode::ready, dfs, &Dfs::startDFS);
    connect(accController, &AccountController::initDfs, dfs, &Dfs::initMyLocalStorage);
    connect(actorIndex, &ActorIndex::initDfs, dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(netManager, &NetManager::newDfsSocket, dfsNetManager, &DFSNetManager::appendSocket);
}

void ExtraChainNode::connectSignals()
{
    connect(this, &ExtraChainNode::ready, []() { qInfo() << "Ready"; });
    connectTxManager();
#ifdef ECONSOLE
    connectConsole();
#endif
    connectResolveManager();
    connectContractManager();
    //    connectAccountController();
    connectActorIndex();
    connectSmContractManager();
    dfsConnection();

    connect(netManager, &NetManager::newSocket, this, &ExtraChainNode::getAllActorsTimerCall);
    connect(this, &ExtraChainNode::getAllActorsNode, actorIndex, &ActorIndex::getAllActors);
}

void ExtraChainNode::prepareFolders()
{
    qDebug() << "Preparing folders";
    qDebug() << "Working directory:" << QDir::currentPath();

    FileSystem::createFolderIfNotExist(KeyStore::USER_KEYSTORE);
    FileSystem::createFolderIfNotExist(DataStorage::TMP_FOLDER);
    FileSystem::createFolderIfNotExist(DataStorage::BLOCKCHAIN_INDEX + "/"
                                       + DataStorage::ACTOR_INDEX_FOLDER_NAME);
    FileSystem::createFolderIfNotExist(DataStorage::BLOCKCHAIN_INDEX + "/"
                                       + DataStorage::BLOCK_INDEX_FOLDER_NAME);
}

int ExtraChainNode::getClientList()
{
    return netManager->getConnections().size();
}

AccountController *ExtraChainNode::getAccountController() const
{
    return accController;
}

ActorIndex *ExtraChainNode::getActorIndex() const
{
    return actorIndex;
}

ResolveManager *ExtraChainNode::getResolveManager() const
{
    return resolveManager;
}

PrivateProfile *ExtraChainNode::getPrivateProfile() const
{
    return prProfile;
}

SubscribeController *ExtraChainNode::getSubscribeController() const
{
    return subscribeController;
}

#ifdef ECLIENT
NotificationManager *ExtraChainNode::getNotificationManager() const
{
    return notificationManager;
}
#endif

void ExtraChainNode::logOut()
{
}

// void ExtraChainNode::createActorWith

// void ExtraChainNode::makeContractFirstTransaction(Contract &contract)
//{
//    qDebug() << "ExtraChainNode::makeContractFirstTransaction";
//    //    contract.setFirst_transaction_hash(
//    //        createTransaction(BigNumber(0), contract.getAmount()).getHash());
//    netManager->shareContract(contract);
//}

// void ExtraChainNode::makeContractFinalTransaction(Contract &contract)
//{
//    contract.setFinal_transaction_hash(
//        createTransaction(contract.getPerformer(), contract.getAmount()).getHash());
//    qDebug() << contract.serialize();
//    contract.setIsCompleted(true);
//    netManager->shareContract(contract);
//}

void ExtraChainNode::tempareSlotForActors()
{
    emit sendActorStateList(accController->getCurrentState());
    emit sendActorToWallet(accController->getAccountID());
}

void ExtraChainNode::coinResponse(ActorId receiver, BigNumber amount, BigNumber plsr)
{
#ifdef ECONSOLE
    auto mainActor = accController->getMainActor();

    if (mainActor == nullptr)
    {
        qDebug() << "Main actor not exists";
        return;
    }

    if (actorIndex->companyId == nullptr)
        return;

    BigNumber companyId = BigNumber(*actorIndex->companyId);
    if (mainActor->id() == companyId)
    {
        qInfo().noquote() << "Company send to" << receiver << "with amount" << amount;
        createTransactionFrom(companyId, receiver, amount);
    }
    else
    {
        if (plsr > 0 && mainActor->id() != plsr)
        {
            return;
        }

        if (blockchain->getUserBalance(mainActor->id(), BigNumber(0)) < amount)
        {
            qInfo().noquote() << "Not enough coins on wallet" << mainActor;
            return;
        }

        m_requestCoinQueue.append({ receiver, amount, plsr });
        if (m_listenCoinRequest)
        {
            return;
        }

        qInfo().noquote() << "Coin request from" << receiver.toByteArray() << "with amount"
                          << Transaction::amountToVisible(amount);
        qInfo() << "Send? (y/n)";
        m_listenCoinRequest = true;
    }
#else
    Q_UNUSED(receiver)
    Q_UNUSED(amount)
    Q_UNUSED(plsr)
#endif
}

QByteArray ExtraChainNode::getIdPrivateProfile() const
{
    return idPrivateProfile;
}

void ExtraChainNode::setIdPrivateProfile(QByteArray id)
{
    idPrivateProfile = id;
}

QByteArray ExtraChainNode::getHashLoginPrivateProfile() const
{
    return hashLoginPrivateProfile;
}

void ExtraChainNode::setHashLoginPrivateProfile(QByteArray hash)
{
    hashLoginPrivateProfile = hash;
}

ChatManager *ExtraChainNode::getChatManager() const
{
    return chatManager;
}

Dfs *ExtraChainNode::getDfs() const
{
    return dfs;
}
