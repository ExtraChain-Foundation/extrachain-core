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
    BigNumber i = 0;
    while (i <= blockchain->getLastBlock().getIndex())
    {
        for (auto j : blockchain->getBlock(BlockParam::Id, i.serialize()).extractTransactions())
        {
            qDebug() << j.toString() << '\n';
        }
        ++i;
    }
    txManager = new TransactionManager(accController, blockchain);
    contractManager = new ContractManager(accController, blockchain);
#ifdef ETALONIUM_CLIENT
    uiController = new UiController();
    uiWallet = uiController->getWallet();
    qDebug() << "========" << uiController;
#endif
    dfs = new Dfs(actorIndex, accController);
    cryptManager = new CryptManager(accController);
    connectSignals();

#ifdef ETALONIUM_CONSOLE
    CreateExtracoin();
//    if (!QFile("blockchain/index/actor/0/0").exists())
//    {
//        Actor<KeyPrivate> company(accController->getActor(0));
//        accController->loadActors();
//        for (int i = 0; i < 10; ++i)
//        {
//            Transaction newTransaction(BigNumber(0), BigNumber(i + 1),
//            BigNumber("56bc75e2d63100000"));
//            newTransaction.setSenderBalance(BigNumber("56bc75e2d63100000"));
//            newTransaction.setReceiverBalance(BigNumber(0));
//            newTransaction.setGas(0);
//            newTransaction.setHop(0);
//            newTransaction.sign(company);
//            newTransaction.verify(company.convertToPublic());
//            txManager->addVerifiedTx(newTransaction);
//        }

//        txManager->makeBlock();
//    }
#endif

    ThreadPool::addThread(blockchain);
    ThreadPool::addThread(actorIndex);
    ThreadPool::addThread(contractManager);
    ThreadPool::addThread(cryptManager);
    ThreadPool::addThread(dfs);
    ThreadPool::addThread(smContractController);
#ifdef ETALONIUM_CONSOLE
    emit accController->initDfs();
#endif
}

Actor<KeyPrivate> NodeManager::CreateExtracoin()
{
    int result = actorIndex->add(BigNumber("0"),
                                 "00010"
                                 "00927bffcfb68515622ac53bc3e7b1c6efed8f55de78dad26eae1f224e1"
                                 "a4048a6baa82b2846f2ae82bab83b636a6c6e00011");

    Actor<KeyPrivate> companyPrKey(
        QByteArray("000100031353e2c69b58a777e367d3f2358303fa0092"
                   "7bffcfb68515622ac53bc3e7b1c6efed8f55de78dad26eae1f224e1a4048a6baa82b2"
                   "846f2ae82bab83b636a6c6e00011"));
    accController->savePrivateActor(companyPrKey);
    if (result != Errors::FILE_ALREADY_EXISTS && result != Errors::FILE_IS_NOT_OPENED)
    {
        qDebug() << "Actor"
                 << "0:353e2c69b58a777e367d3f2358303fa:4ac3b1735ddda843a042661303861fa::"
                 << " was added";
        // todo: Event should be emited only on CREATING new actors, not on
        // RECEIVING new one's make methods:
        // * addActor -> add actor to storage
        // * addNewActor -> add actor to storage and emit event NewActor
    }
    return companyPrKey;
}

void NodeManager::showMessage(QString from, QString message)
{
    qDebug() << from << " " << message;
}

void NodeManager::connectSmContractManager()
{
    connect(smContractController, &SmartContractManager::verifyActor, netManager, &NetManager::NewActor);
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
    connect(smContractController, &SmartContractManager::sendTransactionCreateContract, this,
            &NodeManager::sendTransactionContract);
    connect(this, &NodeManager::sendTransactionContract, netManager, &NetManager::sendNewTx);
#endif
    // connect(smContractController, &SmartContractManager::sendCurrentToken,netManager,
    // &NetManager::NewActor);
}

NodeManager::~NodeManager()
{
    //    netManager->quit();
    //    uiController->quit();

    //    delete uiController;
    delete netManager;
    delete txManager;
    delete blockchain;
    delete accController;
    delete actorIndex;
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
                        .arg(tx.toString(), actor.getId().toString());

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

        accController->sentTxList.add(tx.getHash(), tx.serialize());
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
        if (actor.getId() == 0)
            tx.setSenderBalance(BigNumber(0));
        return this->createTransaction(tx);
    }
    else
    {
        qDebug()
            << QString("Warning: can not create tx to [%1]. There no current user").arg(receiver.toString());
    }
    return Transaction();
}

void NodeManager::CheckBlockCount(BigNumber blockCount, QHostAddress peerAddress)
{
    BigNumber currentBlockCount = blockchain->getLastBlock().getIndex();
    if (currentBlockCount == BigNumber(-1))
        currentBlockCount = 0;
    if (currentBlockCount > blockCount)
        return;
    //    if (currentBlockCount == 0)
    //    {
    netManager->sendGetBlock(BlockParam::Id, BigNumber(0).toString());
    //    }
    while (currentBlockCount <= blockCount)
    {
        currentBlockCount = currentBlockCount + 1;
        netManager->sendGetBlock(BlockParam::Id, currentBlockCount.toString());
        qDebug() << "NodeManager::CheckBlockCount" << currentBlockCount;
    }
}
void NodeManager::makeFirstContractTransaction(Contract contract)
{
    qDebug() << contract.serialize();
    QByteArray hash = createTransaction(BigNumber(0), contract.getAmount()).getHash();
    qDebug() << hash;
    contract.setFirst_transaction_hash(hash);
    qDebug() << contract.serialize();
    contractManager->updateContract(contract);
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
void NodeManager::sendTransactionFromUi(BigNumber reciever, BigNumber amount, BigNumber token)
{
    createTransaction(reciever, amount, token);
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
    QList<QByteArray> walletList;
    for (auto curWallet : accController->getAccounts())
    {
        BigNumber currentId = curWallet->getId();
        if (actorIndex->getActor(currentId).isEmpty())
            break;
        walletList.append(actorIndex->getActor(currentId).getKey()->getPublicKey());
        walletList.append(currentId.toStringDec().toUtf8());

        QByteArray amount = blockchain->getUserBalance(currentId, uiWallet->getCurrentToken()).toByteArray();
        walletList.append(WalletController::toRealNumber(amount));
    }

    uiWallet->updateWalletListModel(&walletList);
}

void NodeManager::updateAvailableWalletList()
{
    qDebug() << "NODE MANAGER: updateAvailableWalletList";
    BigNumber currentId = uiWallet->getCurrentWalletId();
    QList<QByteArray> walletList;
    BigNumber lastId = actorIndex->getLastSavedId();

    for (BigNumber i(1); i <= lastId; ++i)
    {
        Actor<KeyPublic> curActor = actorIndex->getActor(i);
        if (curActor.isEmpty() || currentId == curActor.getId()
            || accController->getCurrentActor().getId() == 0)
            continue;
        walletList.append(curActor.getKey()->getPublicKey());
        walletList.append(curActor.getId().toStringDec().toUtf8());
    }

    uiWallet->updateAvailableListModel(&walletList);
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
    accController->changeUserNum(walletId.serialize());
    uiWallet->setCurrentWalletBalance(blockchain->getUserBalance(walletId, uiWallet->getCurrentToken()));

    // updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
}
#endif

void NodeManager::connectNetManager()
{
    connect(netManager, &NetManager::GetTx, blockchain, &Blockchain::getTxFromBlockchain);
    connect(netManager, &NetManager::GetTxPair, blockchain, &Blockchain::getTxPairFromBlockChain);
    connect(netManager, &NetManager::GetBlock, blockchain, &Blockchain::getBlockFromBlockchain);
    connect(netManager, &NetManager::GetBlockCount, blockchain, &Blockchain::getBlockCount);
    connect(netManager, &NetManager::GetActorCount, blockchain, &Blockchain::getActorCount);
    connect(netManager, &NetManager::CheckBlockExistence, blockchain, &Blockchain::checkBlockExistence);
    connect(netManager, &NetManager::BlockCountResponse, this, &NodeManager::CheckBlockCount);
    connect(netManager, &NetManager::AddBlock, blockchain, &Blockchain::addBlockToBlockchain);
    connect(netManager, &NetManager::NewTx, txManager, &TransactionManager::addTransaction);
    connect(netManager, &NetManager::SendBlockExistence, blockchain, &Blockchain::checkBlockExistence);
}

void NodeManager::connectTxManager()
{

    connect(txManager, &TransactionManager::SendBlock, netManager, &NetManager::Verify);
    connect(txManager, &TransactionManager::VerifyTx, blockchain, &Blockchain::VerifyTx);
    connect(netManager, &NetManager::coinRequest, this, &NodeManager::coinResponse);
    connect(this, &NodeManager::NewTx, netManager, &NetManager::sendNewTx);
}

#ifdef ETALONIUM_CLIENT
void NodeManager::connectUi()
{
    connect(uiController, &UiController::connectToServer, netManager, &NetManager::connectToServer);
    connect(uiController, &UiController::updateNetworkDeviceId, this,
            &NodeManager::createNetManagerIdentificator);

    connect(uiController, &UiController::requestProfile, this, &NodeManager::requestProfile);
    connect(this, &NodeManager::requestProfile, actorIndex, &ActorIndex::requestProfile);
    connect(actorIndex, &ActorIndex::sendProfileToUi, this,
            [this](QString userId, Profile profile) { emit profileToUi(userId, profile); });

    connect(this, &NodeManager::profileToUi, uiController, &UiController::profileUpdated);
    connect(uiController, &UiController::saveProfile, this, [this](Profile profile) {
        Actor<KeyPrivate> *key = accController->getMainActor();
        emit saveProfile(key, profile);
    });
    connect(this, &NodeManager::saveProfile, actorIndex, &ActorIndex::saveProfile);

    // Search (temp)
    connect(uiController->getSearch(), &SearchModel::requestProfiles, actorIndex,
            &ActorIndex::profileToSearch);
    connect(actorIndex, &ActorIndex::sendProfileToSearchToUi, uiController->getSearch(),
            &SearchModel::fromActorIndex);

    //=======================================WALLET=========================================
    connect(uiWallet, &WalletController::sendNewTransaction, this, &NodeManager::sendTransactionFromUi,
            Qt::ConnectionType::QueuedConnection);
    connect(uiWallet, &WalletController::updateWalletToNode, this, &NodeManager::updateWalletInUi);
    connect(uiWallet, &WalletController::createWalletToNode, this, &NodeManager::createWalletInUi);
    connect(uiWallet, &WalletController::changeWalletData, this, &NodeManager::changeWalletIdUi);
    connect(uiWallet->getWalletListModel(), &WalletListModel::changeWalletIdInAccountController,
            accController, &AccountController::changeUserNum);
    connect(uiWallet, &WalletController::sendCoinRequestFromUi, netManager, &NetManager::sendCoinRequest,
            Qt::ConnectionType::QueuedConnection);
    connect(uiWallet, &WalletController::addNewWallet,
            [=]() { accController->savePrivateActor(accController->createActor(false)); });
    connect(accController, &AccountController::editPrivateProfile, [this](QByteArray id) {
        emit editPrivateProfile(getHashLoginPrivateProfile(), getIdPrivateProfile(), id);
    });
    connect(blockchain, &Blockchain::updateLastTransactionList, this, &NodeManager::updateWalletInUi);

    //======================================CONTRACT===========================================
    auto contractsModel = uiController->getContractsModel();
    connect(contractsModel, &ContractsModel::loadContractst, contractManager,
            &ContractManager::loadContractsFrom);
    connect(contractsModel, &ContractsModel::approveByPerformer, contractManager,
            &ContractManager::approveContractByPerformer);
    connect(contractsModel, &ContractsModel::completeByCustomer, contractManager,
            &ContractManager::completeContractByCustomer);
    connect(contractsModel, &ContractsModel::completeByPerformer, contractManager,
            &ContractManager::completeContractByPerformer);
    connect(netManager, &NetManager::qmlNetworkStatus, uiController, &UiController::setNetworkStatus);

    connect(contractsModel, &ContractsModel::newContractToNode, contractManager,
            &ContractManager::createContract);

    //==========================================DFS=========================================
    connect(uiController, &UiController::send, dfs, &Dfs::savedNewData);
    connect(accController, &AccountController::initDfs, dfs, &Dfs::init);
    connect(accController, &AccountController::addActorInActorIndex, this,
            &NodeManager::addActorInActorIndex);
    connect(this, &NodeManager::addActorInActorIndex, actorIndex, &ActorIndex::addActor);
    connect(cryptManager, &CryptManager::sendEncryptData, uiController,
            &UiController::receiveEncryptOrDecryptData);
    connect(uiController, &UiController::sendForEncryptingORDecrypting, cryptManager,
            &CryptManager::recieveData);
    connect(uiController, &UiController::loadPrivateProfile, prProfile, &PrivateProfile::loadPrivateProfile);
    connect(uiController, &UiController::loadProfileForAutologin, prProfile,
            &PrivateProfile::loadProfileForAutoLogin);
    connect(prProfile, &PrivateProfile::sendPublicProfile, uiController, &UiController::loginPrivateProfile);
    connect(uiController, &UiController::savePrivateProfile, prProfile, &PrivateProfile::savePrivateProfile);

    // connect(dfs, &Dfs::requestData, netManager, &NetManager::requestDfsData);
    // connect(uiController, &UiController::profileById, dfs,
    // &Dfs::profileRequest);
    connect(uiController, &UiController::initDfs, dfs, &Dfs::init);
    connect(dfs, &Dfs::usersChanges, uiController, &UiController::dfsChanges);

    //=============================================LOGIN & REG================================
    connect(uiController->getWelcomePage(), &WelcomePage::regStarted, netManager, &NetManager::reserveActor);
    //    connect(uiController->getWelcomePage(),
    //    &WelcomePage::autoLogInStarted, netManager,
    //            &NetManager::connectToServer);

    //=======================================ACCOUNT_CONTROLLER===============================
    connect(accController, &AccountController::newActorIsCreated, uiController,
            &UiController::userRegistrationCompletion);
    connect(accController, &AccountController::newActorIsCreated, this, &NodeManager::updateActors);
    connect(accController, &AccountController::newActorIsCreated, this, &NodeManager::updateWalletInUi);
}
#endif

void NodeManager::connectContractManager()
{
    connect(contractManager, &ContractManager::contractIsCreated, netManager, &NetManager::sendNewContract);
#ifdef ETALONIUM_CLIENT
    connect(netManager->getResolverService(), &ResolverService::contractFromNetwork, contractManager,
            &ContractManager::contractFromNetWork);
#endif

#ifdef ETALONIUM_CONSOLE
    connect(contractManager, &ContractManager::makeFirstContractTransaction, this,
            &NodeManager::makeFirstContractTransaction);
#endif
}

void NodeManager::connectBlockchain()
{
    connect(blockchain, &Blockchain::TxFound, netManager, &NetManager::sendTxResponse);
    connect(blockchain, &Blockchain::TxPairFound, netManager, &NetManager::sendTxPairResponse);
    connect(blockchain, &Blockchain::BlockFound, netManager, &NetManager::sendBlockResponse);
    connect(blockchain, &Blockchain::BlockCount, netManager, &NetManager::sendBlockCountResponse);
    connect(blockchain, &Blockchain::ActorCount, netManager, &NetManager::sendActorCountResponse);
    connect(blockchain, &Blockchain::BlockIsMissing, netManager, &NetManager::continueHandlingNewBlock);
    //    connect(blockchain, &Blockchain::SendMergedBlock, netManager,
    //    &NetManager::sendMergedBlock);
    connect(blockchain, &Blockchain::GenesisBlockCreated, netManager, &NetManager::sendGenesisBlock);
    connect(blockchain, &Blockchain::VerifiedTx, txManager, &TransactionManager::addVerifiedTx);
}

void NodeManager::connectAccountController()
{
    connect(accController, &AccountController::verifyActor, netManager, &NetManager::NewActor);
    connect(accController, &AccountController::newActorIsCreated, this, &NodeManager::updateActors);
}

void NodeManager::connectActorIndex()
{
    connect(actorIndex, &ActorIndex::NewActor, netManager, &NetManager::sendNewActor);
    //    connect(actorIndex, &ActorIndex::NewActor, [=]() {
    //    accController->loadActors(); });
    connect(actorIndex, &ActorIndex::actorIndexUpdated, netManager, &NetManager::sendGetBlockCount);
    connect(actorIndex, &ActorIndex::sendProfile, this, &NodeManager::sendProfile);
    connect(this, &NodeManager::sendProfile, netManager, &NetManager::sendProfile);

    connect(prProfile, &PrivateProfile::setIdProfile, this, &NodeManager::setIdPrivateProfile);
    connect(prProfile, &PrivateProfile::setHashProfile, this, &NodeManager::setHashLoginPrivateProfile);
}

bool NodeManager::dfsConnection()
{
    bool connect9 = connect(netManager, &NetManager::getDfsRequest, dfs, &Dfs::recieveRequest);
    connect(netManager, &NetManager::newDfsPack, dfs, &Dfs::recieve);
    connect(netManager, &NetManager::downloadDfsRequest, dfs, &Dfs::downloadRequset);
    connect(accController, &AccountController::initDfs, dfs, &Dfs::init);
    connect(actorIndex, &ActorIndex::initDfs, dfs, &Dfs::initUser);
    connect(dfs, &Dfs::downloadResponse, netManager, &NetManager::downloadAnswer);
    // connect(dfs, &Dfs::beginTest, this, &NodeManager::DfsTestStart);

    //    connect(netManager, &NetManager::downloadDfsResponse, dfs,
    //    &Dfs::downloadRecieve);

    // send files
    qDebug() << "dfs send request connection"
             << connect(dfs, &Dfs::sendToPeer, netManager, &NetManager::sendDfsMessageTo);
    connect(dfs, &Dfs::sendMessage, netManager, &NetManager::sendDfsPack);
    connect(dfs, &Dfs::sendRequestf, netManager, &NetManager::sendDfsRequest);
    return connect9;
}

void NodeManager::connectSignals()
{
    connectNetManager();
    connectTxManager();
#ifdef ETALONIUM_CLIENT
    connectUi();
#endif
    connectContractManager();
    connectBlockchain();
    connectAccountController();
    connectActorIndex();
    connectSmContractManager();

    // dfs
    if (!dfsConnection())
        qDebug() << "NODE MANGER :"
                 << "one of more from dfs connection have been failed";
}

void NodeManager::prepareFolders()
{
    qDebug() << "Preparing folders";
    qDebug() << "Working directory : " << QFileInfo(".").absolutePath();

    FileSystem::createFolderIfNotExist(KeyStore::USER_KEYSTORE);
    FileSystem::createFolderIfNotExist(DataStorage::TMP_FOLDER);
    FileSystem::createFolderIfNotExist(DataStorage::BLOCKCHAIN_INDEX + "/"
                                       + DataStorage::ACTOR_INDEX_FOLDER_NAME);
    FileSystem::createFolderIfNotExist(DataStorage::BLOCKCHAIN_INDEX + "/"
                                       + DataStorage::BLOCK_INDEX_FOLDER_NAME);
}
void NodeManager::updateActors()
{
    for (BigNumber i = 1; i < accController->getMainActor()->getId(); ++i)
    {
        if (actorIndex->getById(i).isEmpty())
            netManager->sendGetActor(i);
    }
}

int NodeManager::getClientList()
{
    return netManager->getConnections().size();
}

AccountController *NodeManager::getAccController() const
{
    return accController;
}

void NodeManager::createNewActor(QByteArray data, bool accountStatus)
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
        emit sendActorIdSeva(true, accController->getActorIndex()->getLastSavedId());
        this->dfs = new Dfs(actorIndex, accController);
    }
}

// void NodeManager::createActorWith

void NodeManager::makeContractFirstTransaction(Contract &contract)
{
    qDebug() << "NodeManager::makeContractFirstTransaction";
    //    contract.setFirst_transaction_hash(
    //        createTransaction(BigNumber(0), contract.getAmount()).getHash());
    netManager->shareContract(contract);
}

void NodeManager::makeContractFinalTransaction(Contract &contract)
{
    contract.setFinal_transaction_hash(
        createTransaction(contract.getPerformer(), contract.getAmount()).getHash());
    qDebug() << contract.serialize();
    contract.setIsCompleted(true);
    netManager->shareContract(contract);
}

void NodeManager::takePubKeyFordecr(BigNumber actorId)
{
    emit sendKey(actorIndex->getActor(actorId).getKey()->getPublicKey());
}

void NodeManager::takePrKeyFordecr(BigNumber actorId)
{
    emit sendPrivateKey(accController->getActor(actorId).getKey()->getPrivateKey());
}

void NodeManager::tempareSlotForActors()
{
    emit sendActorStateList(accController->getCurrentState());
    emit sendActorToWallet(accController->getAccountID());
}

void NodeManager::coinResponse(BigNumber receiver, BigNumber amount)
{
    createTransaction(receiver, amount);
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
