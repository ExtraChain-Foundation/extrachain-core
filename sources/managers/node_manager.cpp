#include "managers/node_manager.h"

NodeManager::NodeManager()
{
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
    actorIndex->setAccController(accController);
    ThreadPool::addThread(netManager);
    this->thread()->sleep(1);
    blockchain = new Blockchain(accController, fileMode);
    accController->setBlockchain(blockchain);
    txManager = new TransactionManager(accController, blockchain);
    prProfile->setAccountController(accController);
    chatManager = new ChatManager(accController, actorIndex);
    chatManager->setNetManager(netManager);
    //    contractManager = new ContractManager(accController, blockchain);

#ifdef ETALONIUM_CLIENT
    uiController = new UiController();
    uiWallet = uiController->getWallet();
    qDebug() << "========" << uiController;
#endif
    dfs = new Dfs(actorIndex, accController);
    cryptManager = new CryptManager(accController);
    resolveManager = new ResolveManager(actorIndex, blockchain, netManager, txManager, accController, dfs);
    resolveManager->setNode(this);
    resolveManager->setChatManager(chatManager);

    netManager->setResolveManager(resolveManager);
    dfs->initDFSNetManager(resolveManager);
    prProfile->setDfs(dfs);
    actorIndex->setResolveManager(resolveManager);
    connectSignals();
    static QTimer timer;
    connect(&timer, &QTimer::timeout, this, &NodeManager::getAllActorsTimerCall);
    //            [this]() { emit getAllActorsNode(getIdPrivateProfile(), true); });
    timer.start(10000);
    ThreadPool::addThread(blockchain);
    ThreadPool::addThread(actorIndex);
    ThreadPool::addThread(txManager);
    // ThreadPool::addThread(contractManager);
    ThreadPool::addThread(cryptManager);
    ThreadPool::addThread(dfs);
    ThreadPool::addThread(smContractController);
    ThreadPool::addThread(resolveManager);
    ThreadPool::addThread(prProfile);
    ThreadPool::addThread(chatManager);
}

void NodeManager::createCompanyActor(const QString &password)
{
#ifdef ETALONIUM_CONSOLE
    // accController->loadActors("-1");
    Actor<KeyPrivate> company;
    QByteArray consoleHash = Utils::calcKeccak(password.toUtf8());

    if (QDir("keystore/profile").isEmpty())
    {
        company = CreateExtracoin();
        emit savePrivateProfile(consoleHash, company.getId().toActorId());
    }
    else
    {
        // company = *accController->getAccounts()[0];
        emit loadProfileForConsoleLogin(consoleHash);
    }

    if (blockchain->getRecords() <= 0)
    {
        QByteArray td = company.getKey()->sign("test");
        std::cout << company.getKey()->verify("test", td) << std::endl;
        TMP::companyActorId = new QByteArray(company.getId().toByteArray());
        actorIndex->setCompanyId(new QByteArray(company.getId().toByteArray()));

        QMap<BigNumber, BigNumber> tm;
        tm.insert(0, 0);
        blockchain->addBlock(blockchain->createGenesisBlock(company, tm), true);
    }
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
    //    connect(netManager, &NetManager::MsgReceived, resolveManager, &ResolveManager::resolveMessage);
    //    connect(resolveManager, &ResolveManager::coinRequest, this, &NodeManager::coinResponse);
    //    connect(dfs->getDfsNetManager(), &DFSNetManager::newMessage, resolveManager,
    //            &ResolveManager::resolveMessage);
    // TODO: move
    //    connect(resolveManager, &ResolveManager::sendMsg, netManager, &NetManager::sendMessage);
    connect(this, &NodeManager::sendMsg, resolveManager, &ResolveManager::registrateMsg);
    connect(txManager, &TransactionManager::SendBlock, resolveManager, &ResolveManager::registrateMsg);
    //    connect(dfs, &Dfs::newSender, resolveManager, &ResolveManager::registrateMsg);
}

void NodeManager::connectSmContractManager()
{
    //    connect(smContractController, &SmartContractManager::verifyActor, netManager,
    //    &NetManager::NewActor); TODO!!!
    //    connect(smContractController, &SmartContractManager::addContractActorInActorIndex, this,
    //            &NodeManager::addActorInActorIndex);
    connect(smContractController, &SmartContractManager::saveActorInPrivateProfile,
            [this](QByteArray id, QString type, bool rewrite) {
                emit editPrivateProfile(getHashLoginPrivateProfile(), getIdPrivateProfile(), type, id,
                                        rewrite);
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
        if (tx.getSender().toActorId() == *actorIndex->companyId)
            emit NewTx(tx);
        else
            emit sendMsg(tx.serialize(), Messages::TX_MESSAGE);

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

void NodeManager::getAllActors()
{
    QByteArray res = getIdPrivateProfile();
    if (!res.isEmpty())
        emit getAllActorsNode(res, true);
}
void NodeManager::getAllActorsTimerCall()
{
#ifdef ETALONIUM_CLIENT
    QByteArray res = getIdPrivateProfile();
    if (!res.isEmpty())
        emit getAllActorsNode(res, true);
#endif
#ifdef ETALONIUM_CONSOLE
    QByteArray res = accController->getMainActor()->getId().toActorId();
    if (!res.isEmpty())
        emit getAllActorsNode(res, true);
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
#ifdef ETALONIUM_CLIENT
void NodeManager::sendTransactionFromUi(BigNumber receiver, BigNumber amount, BigNumber token)
{
    Transaction tx = this->createTransaction(receiver, amount, token);
}
void NodeManager::createWalletInUi()
{
    // accController->loadActors();
    uiWallet->setCurrentWalletId(accController->getCurrentActor().getId().toActorId());
    uiWallet->setCurrentWalletBalance(
        blockchain->getUserBalance(accController->getCurrentActor().getId(), uiWallet->getCurrentToken()));

    updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
    uiWallet->walletsUpdated();
}

void NodeManager::updateWalletInUi()
{
    uiController->getWallet()->setCurrentWalletId(accController->getCurrentActor().getId().toActorId());
    uiWallet->setCurrentWalletBalance(
        blockchain->getUserBalance(accController->getCurrentActor().getId(), uiWallet->getCurrentToken()));

    updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
    uiWallet->walletsUpdated();
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
    connect(uiController, &UiController::connectToServer, netManager, &NetManager::reconnectUi);
    connect(uiController, &UiController::connectToServer, dfs->getDfsNetManager(),
            &DFSNetManager::uiReconnect);
    connect(uiController, &UiController::updateNetworkDeviceId, this,
            &NodeManager::createNetManagerIdentificator);

    connect(uiController, &UiController::requestProfile, actorIndex, &ActorIndex::requestProfile);
    connect(actorIndex, &ActorIndex::sendProfileToUi, this,
            [this](QString userId, QByteArrayList profile) { emit profileToUi(userId, Profile(profile)); });

    connect(this, &NodeManager::profileToUi, uiController, &UiController::profileUpdated);
    connect(uiController, &UiController::saveProfile, this, [this](QByteArrayList profile) {
        Actor<KeyPrivate> *key = accController->getMainActor();
        emit saveProfile(key, profile);
    });
    connect(this, &NodeManager::saveProfile, actorIndex, &ActorIndex::saveProfile);
    connect(netManager, &NetManager::qmlNetworkStatus, uiController, &UiController::setNetworkStatus);
    connect(netManager, &NetManager::qmlNetworkSockets, uiController, &UiController::setNetworkSockets);

    // Search (temp)
    connect(uiController->getSearch(), &SearchModel::requestProfiles, actorIndex,
            &ActorIndex::profileToSearch);
    connect(actorIndex, &ActorIndex::sendProfileToSearchToUi, uiController->getSearch(),
            &SearchModel::fromActorIndex);

    //=======================================WALLET=========================================
    connect(uiWallet, &WalletController::sendNewTransaction, this, &NodeManager::sendTransactionFromUi);
    connect(uiWallet, &WalletController::updateWalletToNode, this, &NodeManager::updateWalletInUi);
    //    connect(uiWallet, &WalletController::createWalletToNode, this, &NodeManager::createWalletInUi);
    connect(uiWallet, &WalletController::changeWalletData, this, &NodeManager::changeWalletIdUi);
    connect(uiWallet->getWalletListModel(), &WalletListModel::changeWalletIdInAccountController,
            accController, &AccountController::changeUserNum);

    connect(uiWallet, &WalletController::sendCoinRequestFromUi, resolveManager,
            &ResolveManager::registrateMsg);
    connect(uiWallet, &WalletController::addNewWallet, this, &NodeManager::addNewWallet);

    connect(accController, &AccountController::editPrivateProfile, [this](QByteArray id) {
        emit editPrivateProfile(getHashLoginPrivateProfile(), getIdPrivateProfile(), "wallet", id, false);
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
    connect(uiController, &UiController::editInfo, [this](QString value, QByteArray data, bool rewrite) {
        emit editPrivateProfile(getHashLoginPrivateProfile(), getIdPrivateProfile(), value, data, rewrite);
    });
    connect(uiController, &UiController::getInfoFromPrProfile, [this](const QString &type) {
        emit loadInfoFromPrProfile(getHashLoginPrivateProfile(), getIdPrivateProfile(), type);
    });
    connect(this, &NodeManager::loadInfoFromPrProfile, prProfile,
            &PrivateProfile::loadInfoFromPrivateProfile);
    connect(prProfile, &PrivateProfile::infoToUi, uiController, &UiController::loadInfo);
    //    connect(accController, &AccountController::addActorInActorIndex, this,
    //            &NodeManager::addActorInActorIndex);
    //    connect(this, &NodeManager::addActorInActorIndex, actorIndex, &ActorIndex::addActor);
    connect(uiController, &UiController::loadPrivateProfile, prProfile, &PrivateProfile::loadPrivateProfile);
    connect(uiController, &UiController::loadProfileForAutologin, prProfile,
            &PrivateProfile::loadProfileForAutoLogin);
    connect(prProfile, &PrivateProfile::initActorChatM, chatManager, &ChatManager::ActorInit);
    //    connect(prProfile, &PrivateProfile::initActorChatM, this, &NodeManager::getAllActors);
    //            [this]() { emit getAllActorsNode(getIdPrivateProfile(), true); });

    connect(accController, &AccountController::loadWallets, blockchain,
            &Blockchain::updateBlockchainForSignIn);
    connect(accController, &AccountController::savePrivateProfile, this, [=](QByteArray id) {
        setIdPrivateProfile(id);
        emit savePrivateProfile(getHashLoginPrivateProfile(), getIdPrivateProfile());
    });
    connect(accController, &AccountController::savePrivateProfile, chatManager, &ChatManager::ActorInit);
    connect(this, &NodeManager::savePrivateProfile, prProfile, &PrivateProfile::savePrivateProfile);
    connect(accController, &AccountController::loadWallets, uiController, &UiController::loginPrivateProfile);
    connect(uiController, &UiController::logout, accController, &AccountController::clearAcc);
    // connect(dfs, &Dfs::requestData, netManager, &NetManager::requestDfsData);
    // connect(uiController, &UiController::profileById, dfs,
    // &Dfs::profileRequest);
    // connect(uiController, &UiController::initDfs, dfs, &Dfs::init);
    connect(dfs, &Dfs::usersChanges, uiController->getUiResolver(), &UIResolver::resolveMsg);

    //=============================================LOGIN & REG================================
    connect(uiController->getWelcomePage(), &WelcomePage::regStarted, accController,
            [=](QByteArray hash, const bool account) {
                setHashLoginPrivateProfile(hash);
                accController->createActor(1);
            });
    //    connect(uiController->getWelcomePage(),
    //    &WelcomePage::autoLogInStarted, netManager,
    //            &NetManager::connectToServer);

    //=======================================ACCOUNT_CONTROLLER===============================
    connect(accController, &AccountController::newActorIsCreated, uiController,
            &UiController::userRegistrationCompletion);
    connect(accController, &AccountController::newActorIsCreated, this, &NodeManager::updateWalletInUi);
    connect(accController, &AccountController::newActorIsCreated, blockchain, &Blockchain::updateBlockchain);
    connect(accController, &AccountController::newActorIsCreated, actorIndex, &ActorIndex::getAllActors);

    //=============================================CHAT=======================================
    connect(uiController, &UiController::createChat, chatManager, &ChatManager::CreateNewChat);
    connect(uiController, &UiController::inviteToChat, chatManager, &ChatManager::InviteToChat);
    connect(uiController, &UiController::createDialogue, chatManager, &ChatManager::createDialogue);

    connect(uiController, &UiController::sendMessage, chatManager, &ChatManager::SendMessage);
    connect(chatManager, &ChatManager::sendMessage, resolveManager, &ResolveManager::registrateMsg);

    connect(uiController, &UiController::requestChatList, chatManager, &ChatManager::requestChatList);
    connect(uiController, &UiController::requestChat, chatManager, &ChatManager::requestChat);

    connect(chatManager, &ChatManager::chatListSend, uiController, &UiController::chatListReceived);
    connect(chatManager, &ChatManager::chatSend, uiController, &UiController::chatReceived);
    connect(chatManager, &ChatManager::chatCreated, uiController->getChatListModel(),
            &ChatListModel::chatAdded);
    connect(chatManager, &ChatManager::sendLastMessage, uiController->getChatModel(),
            &ChatModel::messageReceived);
    connect(chatManager, &ChatManager::sendLastMessage, uiController->getChatListModel(),
            &ChatListModel::messageReceived);

    connect(uiController, &UiController::removeChat, chatManager, &ChatManager::chatRemoved);

    //
    uiController->startThreads();
}

void NodeManager::addNewWallet()
{
    auto actor = accController->createActor(0);
    accController->savePrivateActor(actor);
    auto wallets = uiWallet->getCurrentWallets();
    uiWallet->setCurrentWallets(wallets << actor.getId().toActorId());
    this->createWalletInUi();
    //    uiWallet->createWalletToNode();
}
#elif ETALONIUM_CONSOLE
void NodeManager::connectConsole()
{
    connect(this, &NodeManager::savePrivateProfile, prProfile, &PrivateProfile::savePrivateProfile);
    connect(this, &NodeManager::loadProfileForConsoleLogin, prProfile,
            &PrivateProfile::loadProfileForAutoLogin);
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
    connect(accController, &AccountController::initDfs, dfs, &Dfs::init);
    connect(actorIndex, &ActorIndex::initDfs, dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(netManager, &NetManager::newDfsSocket, dfsNetManager, &DFSNetManager::appendSocket);
}

void NodeManager::connectSignals()
{
    connectTxManager();
#ifdef ETALONIUM_CLIENT
    connectUi();
#elif ETALONIUM_CONSOLE
    connectConsole();
#endif
    connectResolveManager();
    connectContractManager();
    //    connectAccountController();
    connectActorIndex();
    connectSmContractManager();
    dfsConnection();
    connect(this, &NodeManager::getAllActorsNode, actorIndex, &ActorIndex::getAllActors);
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
