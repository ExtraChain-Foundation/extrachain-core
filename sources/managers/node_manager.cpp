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

#include "managers/node_manager.h"

#include "resolve/resolve_manager.h"

NodeManager::NodeManager()
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
    subscribeController->setNodeManager(this);
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
    notifyM = new NotificationManager();
    ThreadPool::addThread(notifyM);
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
    connect(&getAllActorsTimer, &QTimer::timeout, this, &NodeManager::getAllActorsTimerCall);
    // getAllActorsTimer.start(30000);

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

void NodeManager::createCompanyActor(const QString &email, const QString &password)
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

void NodeManager::initConsoleToken(Transaction tx)
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

Actor<KeyPrivate> NodeManager::CreateCompany(QByteArray consoleHash)
{
    accController->createActor(ActorType::Company, consoleHash);

    return *accController->getMainActor();
}

void NodeManager::showMessage(QString from, QString message)
{
    qDebug() << from << " " << message;
}

void NodeManager::connectResolveManager()
{
    //    connect(netManager, &NetManager::MsgReceived, resolveManager, &ResolveManager::resolveMessage);
    //    connect(resolveManager, &ResolveManager::coinRequest, this, &NodeManager::coinResponse);
    //    connect(dfs->getDfsNetManager(), &DFSNetManager::newMessage, resolveManager,
    //            &ResolveManager::resolveMessage);
    // TODO: move
    //    connect(resolveManager, &ResolveManager::sendMsg, netManager, &NetManager::sendMessage);

    connect(this, &NodeManager::sendMsg, resolveManager, &ResolveManager::registrateMsg);
    connect(txManager, &TransactionManager::SendBlock, resolveManager, &ResolveManager::registrateMsg);
    connect(blockchain, &Blockchain::sendMessage, resolveManager, &ResolveManager::registrateMsg);
    //    connect(dfs, &Dfs::newSender, resolveManager, &ResolveManager::registrateMsg);
}

void NodeManager::connectSmContractManager()
{
    //    connect(smContractController, &SmartContractManager::verifyActor, netManager,
    //    &NetManager::NewActor); TODO!!!
    //    connect(smContractController, &SmartContractManager::addContractActorInActorIndex, this,
    //            &NodeManager::addActorInActorIndex);
    connect(smContractController, &SmartContractManager::saveActorInPrivateProfile,
            [this](const QByteArray &id, const QString &type, const bool &rewrite) {
                emit nodeEditPrivateProfile({ getHashLoginPrivateProfile(), getIdPrivateProfile() }, type, id,
                                            rewrite);
            });

    //[this](QString userId, Profile profile) { emit profileToUi(userId, profile); });
    connect(this, &NodeManager::nodeEditPrivateProfile, prProfile, &PrivateProfile::editPrivateProfile);

    connect(this, &NodeManager::generateSmartContract, smContractController,
            &SmartContractManager::createContractProfile);
    connect(smContractController, &SmartContractManager::sendTransactionCreateContract, resolveManager,
            &ResolveManager::registrateMsg);
    connect(smContractController, &SmartContractManager::initConsoleToken, this,
            &NodeManager::initConsoleToken);

    // connect(smContractController, &SmartContractManager::sendCurrentToken,netManager,
    // &NetManager::NewActor);
}

void NodeManager::connectTxManager()
{
    // TODOD delete later (s)
    connect(this, &NodeManager::NewTx, txManager, &TransactionManager::addTransaction);
}

NodeManager::~NodeManager()
{
    //    netManager->quit();
    //    uiController->quit();

    //    delete uiController;
    // delete netManager;
    delete txManager;
    // delete blockchain;
    delete accController;
    // delete actorIndex;
}

// DFSIndex *NodeManager::getDFSIndex(){
//    return dfsIndex;
//}

Blockchain *NodeManager::getBlockchain()
{
    return blockchain;
}

NetManager *NodeManager::getNetManager()
{
    return netManager;
}

#ifdef ECLIENT
void NodeManager::setNotificationClient(NotificationClient *newNtfCl)
{
    notifyM->setNotifyClient(newNtfCl);
    notifyM->setActorIndex(actorIndex);
    notifyM->setAccController(accController);
}
#endif

Transaction NodeManager::createTransaction(Transaction tx)
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
                        .arg(tx.toString(), QString(actor.id().toActorId()));

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

Transaction NodeManager::createTransaction(BigNumber receiver, BigNumber amount, BigNumber token)
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
                    .arg(QString(receiver.toActorId()));
    return Transaction();
}

Transaction NodeManager::createFreezeTransaction(BigNumber receiver, BigNumber amount, bool toFreeze,
                                                 BigNumber token)
{

    Actor<KeyPrivate> actor = accController->getCurrentActor();

    if (!actor.empty())
    {
        if (receiver == 0)
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
                    .arg(QString(receiver.toActorId()));
    return Transaction();
}

Transaction NodeManager::createTransactionFrom(BigNumber sender, BigNumber receiver, BigNumber amount,
                                               BigNumber token)
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
                        .arg(QString(receiver.toActorId()));
    }
    return Transaction();
}

void NodeManager::getAllActors()
{
    //    QByteArray res = getIdPrivateProfile();
    //    if (!res.isEmpty())
    //        emit getAllActorsNode(res, true);
}
void NodeManager::getAllActorsTimerCall()
{
#ifdef ECLIENT
    QByteArray res = getIdPrivateProfile();
    if (!res.isEmpty())
        emit getAllActorsNode(res, true);
#endif
#ifdef ECONSOLE
    QByteArray res2 = accController->getMainActor()->id().toActorId();
    if (!res2.isEmpty())
        emit getAllActorsNode(res2, true);
#endif
}

void NodeManager::createNetManagerIdentificator()
{
    QFile file(".settings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(BigNumber::random(64).toByteArray());
    file.flush();
    file.close();
}
void NodeManager::dfscreateNetManagerIdentificator()
{
    QFile file(".dsettings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(BigNumber::random(64).toByteArray());
    file.flush();
    file.close();
}
#ifdef ECLIENT
void NodeManager::sendTransactionFromUi(BigNumber receiver, BigNumber amount, BigNumber token)
{
    Transaction tx = this->createTransaction(receiver, amount, token);
}
void NodeManager::createWalletInUi()
{
    return;
    // accController->loadActors();
    uiWallet->setCurrentWalletId(accController->getCurrentActor().id().toActorId());
    uiWallet->setCurrentWalletBalance(
        blockchain->getUserBalance(accController->getCurrentActor().id(), uiWallet->getCurrentToken()));

    updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
    uiWallet->walletsUpdated();
}

void NodeManager::updateWalletInUi()
{
    return;
    // uiController->getWallet()->setCurrentWalletId(accController->getCurrentActor().id().toActorId());
    uiWallet->setCurrentWalletBalance(
        blockchain->getUserBalance(accController->getCurrentActor().id(), uiWallet->getCurrentToken()));

    updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
    uiWallet->walletsUpdated();
}

void NodeManager::updateWalletList()
{
    return;
    QByteArrayList walletList;
    QByteArrayList currentWallets = uiWallet->getCurrentWallets();

    for (const QByteArray &currentId : currentWallets)
    {
        if (actorIndex->getActor(currentId).empty())
            break;

        walletList.append(currentId);

        QByteArray amount = blockchain->getUserBalance(currentId, uiWallet->getCurrentToken()).toByteArray();
        QByteArray stakingMy =
            blockchain->getFreezeUserBalance(currentId, uiWallet->getCurrentToken()).toByteArray();
        QByteArray stakingOther =
            blockchain->getFreezeUserBalance(currentId, uiWallet->getCurrentToken(), -2).toByteArray();
        walletList << Transaction::amountToVisible(amount).toLatin1()
                   << Transaction::amountToVisible(stakingMy).toLatin1()
                   << Transaction::amountToVisible(stakingOther).toLatin1();
    }

    uiWallet->updateWalletListModel(&walletList);
}

void NodeManager::updateAvailableWalletList()
{
    return;
    QByteArray currentId = uiWallet->getCurrentWalletId().toActorId();
    QStringList actors = uiWallet->getAllActor(currentId);

    /*
    QList<QByteArray> walletList;
    Subscribtion sub;
    QList<BigNumber> subActorsList = sub.getAll();

    for (const BigNumber &actor : subActorsList)
    {
        Actor<KeyPublic> curActor = actorIndex->getActor(actor);
        if (curActor.isEmpty() || currentId == curActor.getId()
            || accController->getCurrentActor().getId() == 0)
            continue;
        walletList.append(curActor.getId().toActorId());
    }
    */

    uiWallet->updateAvailableListModel(&actors);
}

void NodeManager::updateRecentActivities()
{
    return;
    QList<Transaction> recentTransactionList;

    recentTransactionList = blockchain->getTxsBySenderOrReceiverInRow(accController->getCurrentActor().id(),
                                                                      -1, 100, uiWallet->getCurrentToken());

    uiWallet->updateRecentActivitiesModel(&recentTransactionList);
}

void NodeManager::changeWalletIdUi(BigNumber walletId)
{
    return;
    // qDebug() << "NODE MANAGER: changeWalletIdUi, id = " << walletId;
    // accController->loadActors();
    accController->changeUserNum(walletId.toActorId());
    uiWallet->setCurrentWalletBalance(blockchain->getUserBalance(walletId, uiWallet->getCurrentToken()));

    // updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
}

void NodeManager::addNewWallet()
{
    return;
    auto future = QtConcurrent::run(accController, &AccountController::createActor, ActorType::Wallet,
                                    hashLoginPrivateProfile);

    AsyncFuture::observe(future).subscribe([this, future]() {
        auto walletId = future.result().id().toActorId();
        auto wallets = uiWallet->getCurrentWallets();
        uiWallet->setCurrentWallets(wallets << walletId);
        createWalletInUi();
    });
}

void NodeManager::notificationToken(QString os, QString actorId, QString token)
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

    sendMsg(Serialization::serializeMap(map), Messages::GeneralRequest::Notification);
}

#elif ECONSOLE
void NodeManager::connectConsole()
{
    connect(this, &NodeManager::savePrivateProfile, prProfile, &PrivateProfile::savePrivateProfile);
    connect(this, &NodeManager::loadProfileForConsoleLogin, prProfile, &PrivateProfile::loadPrivateProfile);
}
#endif

void NodeManager::connectContractManager()
{
}

void NodeManager::connectActorIndex()
{
    connect(actorIndex, &ActorIndex::sendMessage, resolveManager, &ResolveManager::registrateMsg);
    // this connect with service message

    connect(prProfile, &PrivateProfile::setIdProfile, this, &NodeManager::setIdPrivateProfile);
    connect(prProfile, &PrivateProfile::setHashProfile, this, &NodeManager::setHashLoginPrivateProfile);
}

void NodeManager::dfsConnection()
{
    // init dfs for user
    // connect(this, &NodeManager::ready, netManager, &NetManager::startNetwork);
    connect(this, &NodeManager::ready, dfs, &Dfs::startDFS);
    connect(accController, &AccountController::initDfs, dfs, &Dfs::initMyLocalStorage);
    connect(actorIndex, &ActorIndex::initDfs, dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(netManager, &NetManager::newDfsSocket, dfsNetManager, &DFSNetManager::appendSocket);
}

void NodeManager::connectSignals()
{
    connect(this, &NodeManager::ready, []() { qInfo() << "Ready"; });
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

    connect(netManager, &NetManager::newSocket, this, &NodeManager::getAllActorsTimerCall);
    connect(this, &NodeManager::getAllActorsNode, actorIndex, &ActorIndex::getAllActors);
}

void NodeManager::prepareFolders()
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

int NodeManager::getClientList()
{
    return netManager->getConnections().size();
}

AccountController *NodeManager::getAccController() const
{
    return accController;
}

ActorIndex *NodeManager::getActorIndex() const
{
    return actorIndex;
}

ResolveManager *NodeManager::getResolveManager() const
{
    return resolveManager;
}

PrivateProfile *NodeManager::getPrivateProfile() const
{
    return prProfile;
}

SubscribeController *NodeManager::getSubscribeController() const
{
    return subscribeController;
}

void NodeManager::logOut()
{
}

// void NodeManager::createActorWith

// void NodeManager::makeContractFirstTransaction(Contract &contract)
//{
//    qDebug() << "NodeManager::makeContractFirstTransaction";
//    //    contract.setFirst_transaction_hash(
//    //        createTransaction(BigNumber(0), contract.getAmount()).getHash());
//    netManager->shareContract(contract);
//}

// void NodeManager::makeContractFinalTransaction(Contract &contract)
//{
//    contract.setFinal_transaction_hash(
//        createTransaction(contract.getPerformer(), contract.getAmount()).getHash());
//    qDebug() << contract.serialize();
//    contract.setIsCompleted(true);
//    netManager->shareContract(contract);
//}

void NodeManager::tempareSlotForActors()
{
    emit sendActorStateList(accController->getCurrentState());
    emit sendActorToWallet(accController->getAccountID());
}

void NodeManager::coinResponse(BigNumber receiver, BigNumber amount, BigNumber plsr)
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

QByteArray NodeManager::getIdPrivateProfile() const
{
    return idPrivateProfile;
}

void NodeManager::setIdPrivateProfile(QByteArray id)
{
    idPrivateProfile = id;
}

QByteArray NodeManager::getHashLoginPrivateProfile() const
{
    return hashLoginPrivateProfile;
}

void NodeManager::setHashLoginPrivateProfile(QByteArray hash)
{
    hashLoginPrivateProfile = hash;
}

ChatManager *NodeManager::getChatManager() const
{
    return chatManager;
}

Dfs *NodeManager::getDfs() const
{
    return dfs;
}
