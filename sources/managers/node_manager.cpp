#include "managers/node_manager.h"

#include "network/network_manager.h"

NodeManager::NodeManager()
{
    prepareFolders();
    if (!QFile(".settings").exists())
        createNetManagerIdentificator();
    actorIndex = new ActorIndex();
    prProfile = new PrivateProfile();
    smContractController = new SmartContractManager(actorIndex);
    accController = new AccountController(actorIndex);
    netManager = new NetManager(accController, actorIndex);
    ThreadPool::addThread(netManager);
    this->thread()->sleep(1);
    // accController->loadActors();
    blockchain = new Blockchain(accController, fileMode);
    //    BigNumber i = 0;  // UNCOMMENT IF NOT BUILD
    txManager = new TransactionManager(accController, blockchain);
    //    contractManager = new ContractManager(accController, blockchain);

#ifdef ETALONIUM_CLIENT
    uiController = new UiController();
    uiWallet = uiController->getWallet();
    qDebug() << "========" << uiController;
#endif
    dfs = new Dfs(actorIndex, accController);
    cryptManager = new CryptManager(accController);
    resolveManager = new ResolveManager(actorIndex, blockchain, netManager, txManager, accController, dfs);
    connectSignals();

#ifdef ETALONIUM_CONSOLE
    accController->loadActors();
    Actor<KeyPrivate> company;
    if (accController->getAccountCount() == 0)
    {
        company = CreateExtracoin();
    }
    else
    {
        company = *accController->getAccounts()[0];
    }
    QByteArray td = company.getKey()->sign("test");
    std::cout << company.getKey()->verify("test", td) << std::endl;
    TMP::companyActorId = new QByteArray(company.getId().toByteArray());
    actorIndex->setCompanyId(new QByteArray(company.getId().toByteArray()));
    if (blockchain->getRecords() <= 0)
    {
        QMap<BigNumber, BigNumber> tm;
        tm.insert(0, 0);
        blockchain->addBlock(blockchain->createGenesisBlock(company, tm), true);
    }
//    Transaction newTransaction(company.getId(), company.getId(), BigNumber("0"));
//    newTransaction.setSenderBalance(BigNumber("0"));
//    newTransaction.setReceiverBalance(BigNumber("0"));
//    newTransaction.setGas(0);
//    newTransaction.setHop(0);
//    newTransaction.sign(company);
//    newTransaction.verify(company.convertToPublic());
//    txManager->addVerifiedTx(newTransaction);

//    Block block = txManager->makeBlock();
//    blockchain->addBlock(block, true);

//    }
#endif

    ThreadPool::addThread(blockchain);
    ThreadPool::addThread(actorIndex);
    ThreadPool::addThread(txManager);
    // ThreadPool::addThread(contractManager);
    ThreadPool::addThread(cryptManager);
    ThreadPool::addThread(dfs);
    ThreadPool::addThread(smContractController);
    ThreadPool::addThread(resolveManager);
    ThreadPool::addThread(prProfile);

#ifdef ETALONIUM_CONSOLE
    emit accController->initDfs();
#endif
}

Actor<KeyPrivate> NodeManager::CreateExtracoin()
{
    accController->createActor(actorType::COMPANY);

    return *accController->getMainActor();
}

void NodeManager::showMessage(QString from, QString message)
{
    qDebug() << from << " " << message;
}

void NodeManager::connectResolveManager()
{
    connect(netManager, &NetManager::MsgReceived, resolveManager, &ResolveManager::resolveMessage);
    connect(resolveManager, &ResolveManager::coinRequest, this, &NodeManager::coinResponse);
    // TODO: move
    connect(resolveManager, &ResolveManager::sendMsg, netManager, &NetManager::sendMessage);
    connect(this, &NodeManager::sendMsg, resolveManager, &ResolveManager::registrateMsg);
    connect(txManager, &TransactionManager::SendBlock, resolveManager, &ResolveManager::registrateMsg);
    //    connect(dfs, &Dfs::newSender, resolveManager, &ResolveManager::registrateMsg);
}

void NodeManager::connectSmContractManager()
{
    //    connect(smContractController, &SmartContractManager::verifyActor, netManager,
    //    &NetManager::NewActor); TODO!!!
    connect(smContractController, &SmartContractManager::addContractActorInActorIndex, this,
            &NodeManager::addActorInActorIndex);
    connect(smContractController, &SmartContractManager::saveActorInPrivateProfile, [this](QByteArray id) {
        emit editPrivateProfile(getHashLoginPrivateProfile(), getIdPrivateProfile(), id);
    });
    connect(this, &NodeManager::editPrivateProfile, prProfile, &PrivateProfile::editPrivateProfile);
    //[this](QString userId, Profile profile) { emit profileToUi(userId, profile); });

#ifdef ETALONIUM_CLIENT
    connect(uiController, &UiController::generateSmartContract, smContractController,
            &SmartContractManager::createContractProfile);
    connect(smContractController, &SmartContractManager::sendTransactionCreateContract, resolveManager,
            &ResolveManager::registrateMsg);

#endif
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

#ifdef ETALONIUM_CLIENT
UiController *NodeManager::getUiController() const
{
    return uiController;
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
    if (!actor.isEmpty())
    {
        qDebug() << QString("Attempting to create tx:[%1] from user [%2]")
                        .arg(tx.toString(), QString(actor.getId().toActorId()));

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
        qDebug() << tx.toString();
        emit NewTx(tx);

        accController->sentTxList.add(tx.getHash(), Serialization::universalSerialize({ tx.serialize() }, 4));
        return tx;
    }
    else
    {
        qDebug() << QString("Warning: can not create tx:[%1]. There no current user").arg(tx.toString());
    }
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
    if (!actor.isEmpty())
    {
        qDebug() << actor.getId();
        Transaction tx(actor.getId(), receiver, amount);
        // add sent tx balances
        BigNumber tempBalance = 0;

        if (accController->sentTxList.getIndexSize() > 0)
        {
            for (int i = accController->sentTxList.getIndexSize() - 1; i >= 0; i--)
            {
                Transaction tempTx(accController->sentTxList.at(i));
                if (tempTx.getToken() != token)
                    continue;
                if (tempTx.getSender() == actor.getId())
                    tempBalance -= tempTx.getAmount();
                else
                    tempBalance += tempTx.getAmount();
            }
        }

        if (actor.getId() == tx.getSender())
        {
            BigNumber actorBalance = blockchain->getUserBalance(actor.getId(), token);
            BigNumber receiverBalance = blockchain->getUserBalance(receiver, token);
            tx.setSenderBalance(actorBalance + tempBalance);
            tx.setReceiverBalance(receiverBalance - tempBalance);
        }

        tx.setToken(token);
        // tx.setHop(2);
        if (actorIndex->companyId != nullptr)
            if (actor.getId() == BigNumber(*actorIndex->companyId))
                tx.setSenderBalance(BigNumber(0));

        return this->createTransaction(tx);
    }
    else
    {
        qDebug() << QString("Warning: can not create tx to [%1]. There no current user")
                        .arg(QString(receiver.toActorId()));
    }
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
    if (!actor.isEmpty())
    {
        qDebug() << actor.getId();
        Transaction tx(actor.getId(), receiver, amount);
        // add sent tx balances
        BigNumber tempBalance = 0;

        if (accController->sentTxList.getIndexSize() > 0)
        {
            for (int i = accController->sentTxList.getIndexSize() - 1; i >= 0; i--)
            {
                Transaction tempTx(accController->sentTxList.at(i));
                if (tempTx.getToken() != token)
                    continue;
                if (tempTx.getSender() == actor.getId())
                    tempBalance -= tempTx.getAmount();
                else
                    tempBalance += tempTx.getAmount();
            }
        }

        if (actor.getId() == tx.getSender())
        {
            BigNumber actorBalance = blockchain->getUserBalance(actor.getId(), token);
            BigNumber receiverBalance = blockchain->getUserBalance(receiver, token);
            tx.setSenderBalance(actorBalance + tempBalance);
            tx.setReceiverBalance(receiverBalance - tempBalance);
        }

        tx.setToken(token);
        // tx.setHop(2);
        if (actorIndex->companyId != nullptr)
            if (actor.getId() == BigNumber(*actorIndex->companyId))
                tx.setSenderBalance(BigNumber(0));
        return this->createTransaction(tx);
    }
    else
    {
        qDebug() << QString("Warning: can not create tx to [%1]. There no current user")
                        .arg(QString(receiver.toActorId()));
    }
    return Transaction();
}

void NodeManager::createNetManagerIdentificator()
{
    QFile file(".settings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(BigNumber::random(64).toByteArray());
    file.flush();
    file.close();
}

#ifdef ETALONIUM_CLIENT
void NodeManager::sendTransactionFromUi(BigNumber receiver, BigNumber amount, BigNumber token)
{
    Actor<KeyPrivate> actor = accController->getCurrentActor();
    if (!actor.isEmpty())
    {
        qDebug() << actor.getId();
        Transaction tx(actor.getId(), receiver, amount);
        // add sent tx balances
        BigNumber tempBalance = 0;

        if (accController->sentTxList.getIndexSize() > 0)
        {
            for (int i = accController->sentTxList.getIndexSize() - 1; i >= 0; i--)
            {
                Transaction tempTx(accController->sentTxList.at(i));
                if (tempTx.getToken() != token)
                    continue;
                if (tempTx.getSender() == actor.getId())
                    tempBalance -= tempTx.getAmount();
                else
                    tempBalance += tempTx.getAmount();
            }
        }

        BigNumber actorBalance = blockchain->getUserBalance(actor.getId(), token);
        BigNumber receiverBalance = blockchain->getUserBalance(receiver, token);
        tx.setSenderBalance(actorBalance + tempBalance);
        tx.setReceiverBalance(receiverBalance - tempBalance);

        tx.setToken(token);
        emit sendMsg(tx.serialize(), Messages::TX_MESSAGE);
    }
}
void NodeManager::createWalletInUi()
{
    // accController->loadActors();
    uiWallet->setCurrentWalletId(accController->getCurrentActor().getId());
    uiWallet->setCurrentWalletBalance(
        blockchain->getUserBalance(accController->getCurrentActor().getId(), uiWallet->getCurrentToken()));

    updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
}

void NodeManager::updateWalletInUi()
{
    //    uiController->getWallet()->setCurrentWalletId(
    //            accController->getCurrentActor().getId());
    uiWallet->setCurrentWalletBalance(
        blockchain->getUserBalance(accController->getCurrentActor().getId(), uiWallet->getCurrentToken()));

    updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
}

void NodeManager::updateWalletList()
{
    QByteArrayList walletList;
    QByteArrayList currentWallets = uiWallet->getCurrentWallets();

    for (const QByteArray &currentId : currentWallets)
    {
        if (actorIndex->getActor(currentId).isEmpty())
            break;

        walletList.append(currentId);

        QByteArray amount = blockchain->getUserBalance(currentId, uiWallet->getCurrentToken()).toByteArray();
        walletList.append(WalletController::toRealNumber(amount));
    }

    uiWallet->updateWalletListModel(&walletList);
}

void NodeManager::updateAvailableWalletList()
{
    qDebug() << "NODE MANAGER: updateAvailableWalletList";
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
    QList<Transaction> recentTransactionList;

    recentTransactionList = blockchain->getTxsBySenderOrReceiverInRow(
        accController->getCurrentActor().getId(), -1, 100, uiWallet->getCurrentToken());

    uiWallet->updateRecentActivitiesModel(&recentTransactionList);
}

void NodeManager::changeWalletIdUi(BigNumber walletId)
{
    qDebug() << "NODE MANAGER: changeWalletIdUi, id = " << walletId;
    // accController->loadActors();
    accController->changeUserNum(walletId.toActorId());
    uiWallet->setCurrentWalletBalance(blockchain->getUserBalance(walletId, uiWallet->getCurrentToken()));

    // updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
}

void NodeManager::connectUi()
{
    connect(uiController, &UiController::connectToServer, netManager, &NetManager::connectToServer);
    connect(uiController, &UiController::updateNetworkDeviceId, this,
            &NodeManager::createNetManagerIdentificator);

    connect(uiController, &UiController::requestProfile, this, &NodeManager::requestProfile);
    connect(this, &NodeManager::requestProfile, actorIndex, &ActorIndex::requestProfile);
    connect(actorIndex, &ActorIndex::sendProfileToUi, this,
            [this](QString userId, QByteArrayList profile) { emit profileToUi(userId, Profile(profile)); });

    connect(this, &NodeManager::profileToUi, uiController, &UiController::profileUpdated);
    connect(uiController, &UiController::saveProfile, this, [this](QByteArrayList profile) {
        Actor<KeyPrivate> *key = accController->getMainActor();
        emit saveProfile(key, profile);
    });
    connect(this, &NodeManager::saveProfile, actorIndex, &ActorIndex::saveProfile);
    connect(netManager, &NetManager::qmlNetworkStatus, uiController, &UiController::setNetworkStatus);

    // Search (temp)
    connect(uiController->getSearch(), &SearchModel::requestProfiles, actorIndex,
            &ActorIndex::profileToSearch);
    connect(actorIndex, &ActorIndex::sendProfileToSearchToUi, uiController->getSearch(),
            &SearchModel::fromActorIndex);

    //=======================================WALLET=========================================
    connect(uiWallet, &WalletController::sendNewTransaction, this, &NodeManager::sendTransactionFromUi);
    connect(uiWallet, &WalletController::updateWalletToNode, this, &NodeManager::updateWalletInUi);
    connect(uiWallet, &WalletController::createWalletToNode, this, &NodeManager::createWalletInUi);
    connect(uiWallet, &WalletController::changeWalletData, this, &NodeManager::changeWalletIdUi);
    connect(uiWallet->getWalletListModel(), &WalletListModel::changeWalletIdInAccountController,
            accController, &AccountController::changeUserNum);

    connect(uiWallet, &WalletController::sendCoinRequestFromUi, resolveManager,
            &ResolveManager::registrateMsg);
    connect(uiWallet, &WalletController::addNewWallet, [=]() { // TODO: to thread!
        auto actor = accController->createActor(0);
        accController->savePrivateActor(actor);
        auto wallets = uiWallet->getCurrentWallets();
        uiWallet->setCurrentWallets(wallets << actor.getId().toActorId());
        uiWallet->createWalletToNode();
    });

    connect(accController, &AccountController::editPrivateProfile, [this](QByteArray id) {
        emit editPrivateProfile(getHashLoginPrivateProfile(), getIdPrivateProfile(), id);
    });
    connect(blockchain, &Blockchain::updateLastTransactionList, this, &NodeManager::updateWalletInUi);
    connect(blockchain, &Blockchain::sendMessage, resolveManager, &ResolveManager::registrateMsg);

    //======================================CONTRACT===========================================
    /*
    auto contractsModel = uiController->getContractsModel();
    connect(contractsModel, &ContractsModel::loadContractst, contractManager,
            &ContractManager::loadContractsFrom);
    connect(contractsModel, &ContractsModel::approveByPerformer, contractManager,
            &ContractManager::approveContractByPerformer);
    connect(contractsModel, &ContractsModel::completeByCustomer, contractManager,
            &ContractManager::completeContractByCustomer);
    connect(contractsModel, &ContractsModel::completeByPerformer, contractManager,
            &ContractManager::completeContractByPerformer);
    connect(contractsModel, &ContractsModel::newContractToNode, contractManager,
            &ContractManager::createContract);
    */

    //==========================================DFS=========================================
    connect(uiController, &UiController::send, dfs, &Dfs::savedNewData);
    //    connect(accController, &AccountController::addActorInActorIndex, this,
    //            &NodeManager::addActorInActorIndex);
    //    connect(this, &NodeManager::addActorInActorIndex, actorIndex, &ActorIndex::addActor);
    connect(uiController, &UiController::loadPrivateProfile, prProfile, &PrivateProfile::loadPrivateProfile);
    connect(uiController, &UiController::loadProfileForAutologin, prProfile,
            &PrivateProfile::loadProfileForAutoLogin);
    connect(prProfile, &PrivateProfile::initPrivateProfile, accController, &AccountController::loadActors);
    connect(accController, &AccountController::loadWallets, blockchain,
            &Blockchain::updateBlockchainForSignIn);
    connect(uiController, &UiController::savePrivateProfile, prProfile, &PrivateProfile::savePrivateProfile);
    connect(accController, &AccountController::loadWallets, uiController, &UiController::loginPrivateProfile);
    connect(uiController, &UiController::logout, accController, &AccountController::clearAcc);
    // connect(dfs, &Dfs::requestData, netManager, &NetManager::requestDfsData);
    // connect(uiController, &UiController::profileById, dfs,
    // &Dfs::profileRequest);
    connect(uiController, &UiController::initDfs, dfs, &Dfs::init);
    connect(dfs, &Dfs::usersChanges, uiController->getUiResolver(), &UIResolver::resolveMsg);

    //=============================================LOGIN & REG================================
    connect(uiController->getWelcomePage(), &WelcomePage::regStarted, accController,
            [=]() { accController->createActor(1); });
    //    connect(uiController->getWelcomePage(),
    //    &WelcomePage::autoLogInStarted, netManager,
    //            &NetManager::connectToServer);

    //=======================================ACCOUNT_CONTROLLER===============================
    connect(accController, &AccountController::newActorIsCreated, uiController,
            &UiController::userRegistrationCompletion);
    connect(accController, &AccountController::newActorIsCreated, this, &NodeManager::updateWalletInUi);
    connect(accController, &AccountController::newActorIsCreated, blockchain, &Blockchain::updateBlockchain);

    uiController->startThreads();
}
#endif

void NodeManager::connectContractManager()
{
}

void NodeManager::connectAccountController()
{
    // connect(accController, &AccountController::verifyActor, netManager, &NetManager::NewActor);
    connect(accController, &AccountController::addActorInActorIndex, this,
            &NodeManager::addActorInActorIndex);
    connect(this, &NodeManager::addActorInActorIndex, actorIndex, &ActorIndex::addActor);
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
    connect(accController, &AccountController::initDfs, dfs, &Dfs::init);
    connect(actorIndex, &ActorIndex::initDfs, dfs, &Dfs::initUser);
    connect(dfs, &Dfs::newSender, resolveManager, &ResolveManager::registrateMsg);
    connect(dfs, &Dfs::newSenderToPeer, netManager, &NetManager::dfsToPeerTmp);
}

void NodeManager::connectSignals()
{
    connectTxManager();
#ifdef ETALONIUM_CLIENT
    connectUi();
#endif
    connectResolveManager();
    connectContractManager();
    connectAccountController();
    connectActorIndex();
    connectSmContractManager();
    dfsConnection();
}

void NodeManager::prepareFolders()
{
    qDebug() << "Preparing folders";
    qDebug() << "Working directory : " << QDir::currentPath();

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

void NodeManager::createNewActor(QByteArray data, int accountStatus)
{
    if (data.isEmpty())
    {
        qDebug() << "slot NodeManagerLocal::createNewActor";
        Actor<KeyPrivate> createdActor = accController->createActor(accountStatus);
        qDebug() << "pk = " << createdActor.getKey()->getPrivateKey();
        accController->savePrivateActor(createdActor);
    }
    if (!data.isEmpty())
    {
        this->dfs = new Dfs(actorIndex, accController);
    }
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

void NodeManager::coinResponse(BigNumber receiver, BigNumber amount)
{
    createTransactionFrom(BigNumber(*actorIndex->companyId), receiver, amount);
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
