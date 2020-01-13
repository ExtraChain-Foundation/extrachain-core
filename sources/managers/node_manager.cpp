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
    subscribeController = new SubscribeController();
    subscribeController->setNodeManager(this);
    actorIndex->setAccController(accController);
    ThreadPool::addThread(netManager);
    //    this->thread()->sleep(1);
    blockchain = new Blockchain(accController, fileMode);
    accController->setBlockchain(blockchain);
    txManager = new TransactionManager(accController, blockchain);
    prProfile->setAccountController(accController);
    chatManager = new ChatManager(accController, actorIndex);
    chatManager->setNetManager(netManager);
    //    contractManager = new ContractManager(accController, blockchain);

#ifdef ETALONIUM_CLIENT
    uiController = new UiController(this);
    uiController->setSubscribeController(subscribeController);
    uiWallet = uiController->getWallet();
    qDebug() << "========" << uiController;
#endif
    dfs = new Dfs(actorIndex, accController);
    cryptManager = new CryptManager(accController);
    resolveManager = new ResolveManager(actorIndex, blockchain, netManager, txManager, accController);
    resolveManager->setNode(this);
    resolveManager->setChatManager(chatManager);
    blockchain->setTxManager(txManager);
    netManager->setResolveManager(resolveManager);
    //    dfs->initDFSNetManager(resolveManager);
    prProfile->setDfs(dfs);
    actorIndex->setResolveManager(resolveManager);
    connectSignals();

    //    static QTimer getAllActorsTimer;
    //    connect(&getAllActorsTimer, &QTimer::timeout, this, &NodeManager::getAllActorsTimerCall);
    //    getAllActorsTimer.start(10000);

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

void NodeManager::createCompanyActor(const QString &email, const QString &password)
{
#ifdef ETALONIUM_CONSOLE
    // accController->loadActors("-1");
    Actor<KeyPrivate> company;
    QByteArray consoleHash = Utils::calcKeccak(email.toUtf8() + password.toUtf8());

    if (QDir("keystore/profile").isEmpty())
    {
        company = CreateExtracoin();
        emit savePrivateProfile(consoleHash, company.getId().toActorId());
    }
    else
    {
        // company = *accController->getAccounts()[0];
        emit loadProfileForConsoleLogin(email.toLatin1(), password.toLatin1());
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
            [this](const QByteArray &id, const QString &type, const bool &rewrite) {
                emit nodeEditPrivateProfile({ getHashLoginPrivateProfile(), getIdPrivateProfile() }, type, id,
                                            rewrite);
            });

    //[this](QString userId, Profile profile) { emit profileToUi(userId, profile); });
    connect(this, &NodeManager::nodeEditPrivateProfile, prProfile, &PrivateProfile::editPrivateProfile);

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
            emit sendMsg(tx.serialize(), Messages::ChainMessage::txMessage);

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
        walletList.append(Transaction::amountToVisible(amount).toLatin1());
    }

    uiWallet->updateWalletListModel(&walletList);
}

void NodeManager::updateAvailableWalletList()
{
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
    // qDebug() << "NODE MANAGER: changeWalletIdUi, id = " << walletId;
    // accController->loadActors();
    accController->changeUserNum(walletId.toActorId());
    uiWallet->setCurrentWalletBalance(blockchain->getUserBalance(walletId, uiWallet->getCurrentToken()));

    // updateWalletList();
    updateAvailableWalletList();
    updateRecentActivities();
}

void NodeManager::connectUi()
{
    connect(uiController, &UiController::authEnded, [this]() { emit this->ready(); });
    connect(uiController, &UiController::connectToServer, netManager, &NetManager::reconnectUi);
    connect(uiController, &UiController::connectToServer, dfs, &Dfs::connectToServer);
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

    connect(uiController, &UiController::initSubscribe, subscribeController,
            &SubscribeController::initSubscribe);
    connect(uiController, &UiController::subscribe, subscribeController,
            &SubscribeController::editMySubscribe);

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
        emit nodeEditPrivateProfile({ getHashLoginPrivateProfile(), getIdPrivateProfile() }, "wallet", id,
                                    false);
        qDebug() << "1111111111111111111";
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
    connect(uiController, &UiController::send, dfs, &Dfs::save);
    connect(chatManager, &ChatManager::send, dfs, &Dfs::save);
    connect(uiController, &UiController::sendEdit, dfs, &Dfs::editData);
    connect(uiController, &UiController::sendEditSql, dfs, &Dfs::editSqlDatabase);
    connect(uiController, &UiController::editInfo, [this](QString value, QByteArray data, bool rewrite) {
        emit nodeEditPrivateProfile({ getHashLoginPrivateProfile(), getIdPrivateProfile() }, value, data,
                                    rewrite);
        qDebug() << "222222222222";
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
    connect(dfs, &Dfs::fileChanged, chatManager, &ChatManager::changes);
    connect(chatManager, &ChatManager::requestFile, dfs, &Dfs::requestFile);
    connect(uiController->getUiResolver(), &UIResolver::loadChat, chatManager, &ChatManager::fileLoaded);
    connect(uiController, &UiController::requestFile, dfs, &Dfs::requestFile);
    connect(uiController, &UiController::authEnded, chatManager, &ChatManager::initChat);

    connect(subscribeController, &SubscribeController::send, dfs, &Dfs::save);
    connect(subscribeController, &SubscribeController::sendEditSql, dfs, &Dfs::editSqlDatabase);
    connect(chatManager, &ChatManager::sendEditSql, dfs, &Dfs::editSqlDatabase);

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

    connect(uiController, &UiController::sendChatFile, chatManager, &ChatManager::sendChatFile);
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

#include "asyncfuture.h" // temp
#include <QtConcurrent>  // temp

void NodeManager::addNewWallet()
{
    auto future = QtConcurrent::run(accController, &AccountController::createActor, 0);

    AsyncFuture::observe(future).subscribe([this, future]() {
        auto walletId = future.result().getId().toActorId();
        auto wallets = uiWallet->getCurrentWallets();
        uiWallet->setCurrentWallets(wallets << walletId);
        createWalletInUi();
    });
}
#elif ETALONIUM_CONSOLE
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
    connect(this, &NodeManager::ready, dfs, &Dfs::startDFS);
    connect(accController, &AccountController::initDfs, dfs, &Dfs::init);
    connect(actorIndex, &ActorIndex::initDfs, dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(netManager, &NetManager::newDfsSocket, dfsNetManager, &DFSNetManager::appendSocket);
}

void NodeManager::connectSignals()
{
    connect(this, &NodeManager::ready, []() { qInfo() << "Ready"; });
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
    //    connect(this, &NodeManager::getAllActorsNode, actorIndex, &ActorIndex::getAllActors);
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
#ifdef ETALONIUM_CONSOLE
    auto mainActor = accController->getMainActor();

    if (mainActor == nullptr)
    {
        qDebug() << "Main actor not exists";
        return;
    }

    BigNumber companyId = BigNumber(*actorIndex->companyId);
    if (mainActor->getId() == companyId)
    {
        qInfo().noquote() << "Company send to" << receiver << "with amount" << amount;
        createTransactionFrom(companyId, receiver, amount);
    }
    else
    {
        if (plsr > 0 && mainActor->getId() != plsr)
        {
            return;
        }

        if (blockchain->getUserBalance(mainActor->getId()) < amount)
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
