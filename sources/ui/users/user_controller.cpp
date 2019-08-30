#include "extracoin/headers/ui/users/user_controller.h"
#include "extracoin/headers/ui/wallet/wallecontrollert.h"

#include <QImage>
#include <QDebug>
#include <QBrush>
#include <QPen>
#include <QPainter>
#include <QImageReader>

#include "extracoin/headers/ui/ui_package.h"

void tempareWriteToFile()
{
    QFile file("path.dat");
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream fin(&file);
    for (int i = 0; i < 100; i++)
    {
        int k = i % 5;
        fin << Serialization::serialize({ "1", QString::number(k).toUtf8(), "10", "test", "0",
                                          "1000000", "10", "0", "0", "5",
                                          Utils::calcKeccak("somethings"), "231",
                                          Utils::calcKeccak("anythink") },
                                        Serialization::TX_FIELD_SPLITTER)
                + "\n";
    }
    file.close();
}

Message *UserController::getMessage()
{
    return this->message;
}

UserController::UserController(QObject *parent)
    : QObject(parent)
{
    tempareWriteToFile();
    newsPage = new NewsModel();
    lifePage = new NewsModel();
    profile = new Profile();
    searchModel = new SearchModel();
    message = new Message();
    eventModel = new EventsModel();
    myEventModel = new EventsModel();
    contractsModel = new ContractsModel();
    portfolioModel = new PortfolioModel();
    //    chatslist = new ChatModel();
    chat = new Chat();
    wallet = new WalletController();
    welcomePage = new WelcomePage();
    // new wallet test
    //    recentActivitiesModel = new RecentActivitiesModel(currentActorId);
    //    walletListModel = new WalletListModel();
    //    availableWalletsModel = new AvailableWalletsModel();
    //

    connecSignals();
}

UserController::~UserController()
{
    emit finished();
}

NewsModel *UserController::getFeed()
{
    return this->newsPage;
}

NewsModel *UserController::getLife()
{
    return this->lifePage;
}

Profile *UserController::getProfile()
{
    return this->profile;
}

WalletController *UserController::getWallet()

{
    return this->wallet;
}

SearchModel *UserController::getSearch()
{
    return this->searchModel;
}

ChatModel *UserController::getChats()
{
    return this->chatslist;
}

Chat *UserController::getChat()
{
    return chat;
}

EventsModel *UserController::getEvent()
{
    return eventModel;
}

WelcomePage *UserController::getWelcomePage() const
{
    return welcomePage;
}

void UserController::setProfile(QByteArray path)
{
    emit getPathProfile(path);
}

void UserController::connecSignals()
{
    // registration
    connect(this, &UserController::getPathProfile, profile, &Profile::recivePathToProfile);
    connect(this, &UserController::setId, this, &UserController::settingId);
    connect(portfolioModel, &PortfolioModel::save, this, &UserController::addPortfolio);

    // avatar
    connect(this, &UserController::addAvatar, this, &UserController::addingAvatar);
    connect(this, &UserController::subscribe, this, &UserController::subscribing);

    // loaded new add life post
    connect(newsPage, &NewsModel::addPost, this, &UserController::addPost);

    connect(newsPage, &NewsModel::requestLastNewsFrom, this,
            &UserController::retranslateSignalfromNewForNews);
    connect(this, &UserController::getPathToLastNews, newsPage, &NewsModel::getNewBack);
    connect(this, &UserController::signalForLifePost, lifePage, &NewsModel::getNewBack);

    // search page
    connect(this, &UserController::sendAllPathToProFile, searchModel,
            &SearchModel::resultSearch);
    connect(searchModel, &SearchModel::startSearch, this,
            &UserController::requestAllPathProfile);

    // chat
    connect(this, &UserController::signalToQMLMessage, message,
            &Message::receiveMessageFromFile);
    connect(message, &Message::sendToDfsForSaving, this, &UserController::ecnryptMassage);
    connect(message, &Message::sendToDfsForReading, this,
            &UserController::retranslateMessageToReadFromDFS);
    //    Chat el = chats->getChat();
    // new chatModel
    connect(chat, &Chat::sendMessage, this,
            &UserController::saveMessageInDFS); // write message to file to DFS
    connect(this, &UserController::sendMessageToChat, chat,
            &Chat::recieveMessage); // new message from DFS
    qDebug() << "--------------------------------------------------\n--------------------"
                "------------------------------\n----------------------------------------"
                "----------\n";
    qDebug() << connect(chat, &Chat::requestMessageList, this,
                        &UserController::readAndDecreptMessageList,
                        Qt::DirectConnection); // loaded messages from file
    qDebug() << "--------------------------------------------------\n--------------------"
                "------------------------------\n----------------------------------------"
                "----------\n";
    connect(chat, &Chat::saveMessageDFS, this, &UserController::saveMyMessageToDFS);
    connect(this, &UserController::messageListToChat, chat, &Chat::recieveMessageList);

    // events

    // connect(this, &UserController::sendEventsToUI, eventModel, &EventsModel::allEvent);
    // connect(eventModel, &EventsModel::requestEvents, this,
    // &UserController::requestEvents); return message List from UserController

    // welcomePage connection
    connect(welcomePage, &WelcomePage::logInStarted, this, &UserController::logIn);
    connect(welcomePage, &WelcomePage::avtoLogInStarted, this, &UserController::avtoLogIn);
    // WalletConnection
    connect(this, &UserController::regEnded, wallet, &WalletController::updateWallet);
}

void UserController::retranslateSignalfromNewForNews(int i, QString actorId)
{
    qDebug() << "void UserController::retranslateSignalfromNewForNews(int i, QString actorId)"
             << actorId;
    emit requestNewsPath(i, BigNumber(actorId.toUtf8()));
}

void UserController::retransalateRequestProfile(BigNumber actorId)
{
    qDebug() << "void UserController::retransalateRequestProfile(BigNumber actorId):"
             << actorId;
    emit requestProfile(actorId);
}

void UserController::sendEvetsFromDFS(QList<QJsonDocument> list)
{
    QVariantList sendList;

    while (!list.isEmpty())
        sendList.append(list.takeFirst().toVariant());
    qDebug() << "slot in UserController" << sendList;
    emit sendEventsToUI(sendList);
}

void UserController::requestEvents(QString actorId)
{
    qDebug() << "sslot in User Controler Activet";
    emit requestEventssignal(BigNumber(actorId.toUtf8()));
}

void UserController::addLifePost(QStringList images, QByteArray post)
{
    qDebug() << "UserController::addLifePost: " << images
             << "\nactor:" << this->currentActorId;
    emit saveLifePost(this->currentActorId, images, post);
}

void UserController::postDataResiver(QStringList images, QString path)
{
    qDebug() << " UserController::postDataResiver::" << images << "\n" << path;
    emit sendPostToNewsModel(images, path);
}

QVariantMap UserController::loadProfile(const QString &userId)
{
    qDebug() << "loadProfile" << userId;
    QFile profileFile("data/" + userId + "/servic/profile.dat");
    if (profileFile.exists())
    {
        profileFile.open(QFile::ReadOnly);
        QJsonDocument profileJson = QJsonDocument::fromJson(profileFile.readAll());
        auto profileMap = profileJson.toVariant().toMap();
        profileFile.close();
        profileMap["userId"] = userId;
        return profileMap;
    }
    else
    {
        return QVariantMap();
    }
}

void UserController::saveProfile(const QVariantMap &profileMap)
{
    QByteArray profile = QJsonDocument::fromVariant(profileMap).toJson(QJsonDocument::Compact);
    welcomePage->setProfileData(
        QJsonDocument::fromVariant(profileMap).toJson(QJsonDocument::Compact));
    universalSender(ui_messages::registation, based_dfs_struct::NEWSTATE, "");
}

void UserController::addingAvatar(const QString &imageFile, bool temp, const int x,
                                  const int y, const int size)
{
    const int avaSize = 150;
    qDebug() << "addingAvatar" << currentActorId.toString() << QUrl(imageFile).toLocalFile()
             << x << y << size;

    QImageReader reader(QUrl(imageFile).toLocalFile());
    reader.setAutoTransform(true);
    QImage originalImage = reader.read();
    QSize originalSize = originalImage.size();

    QImage image = originalImage.copy(x, y, size, size);
    image = image.scaled(avaSize, avaSize);
    QImage out(image.width(), image.height(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter painter(&out);

    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    QPen pen;
    pen.setStyle(Qt::NoPen);
    painter.setPen(pen);
    QBrush brush(image);

    painter.setBrush(brush);

    qreal radius = avaSize / 2;
    painter.drawRoundedRect(QRectF(0, 0, avaSize, avaSize), radius, radius);

    QPixmap pxDst(originalSize);
    pxDst.fill(Qt::transparent);
    QPainter painter2(&pxDst);
    // qt_blurImage(&painter2, out, 8, true, false);

    qDebug() << "AVASLAVA"
             << out.save("data/" + currentActorId.toString() + "/servic/avatar_temp",
                         "PNG"); // 1123

    QFile file2("data/" + currentActorId.toString() + "/servic/avatar_temp");
    file2.open(QFile::ReadOnly);
    QByteArray miniAvatar = file2.readAll(); // TODO: file2
    file2.close();
    qDebug() << (file2.remove() ? "temp removed" : " temp not removed");

    QFile file(temp ? imageFile : QUrl(imageFile).toLocalFile());
    file.open(QFile::ReadOnly);
    QByteArray bigAvatar = file.readAll();
    file.close();

    BigNumber newImage = CardManager::getNameForNewFile(based_dfs_struct::Type::images);

    imagesTempData = bigAvatar;
    universalSender(ui_messages::page::images, based_dfs_struct::State::NEWSTATE, "");
    imagesTempData = "";

    imagesTempData = miniAvatar;
    universalSender(ui_messages::page::miniAva, based_dfs_struct::State::NEWSTATE, "");
    imagesTempData = "";

    auto profileMap = loadProfile(currentActorId.toString());

    if (profileMap["avatar"].isNull())
        profileMap["avatar"] = QStringList();

    QStringList avatars = profileMap["avatar"].toStringList();
    avatars.insert(0, (newImage).toString());
    profileMap["avatar"] = avatars;

    saveProfile(profileMap);
    updateAvatarImage();
}

void UserController::settingId(QByteArray newId)
{
    qDebug() << "[UserController] Set id =" << newId;
    currentActorId = BigNumber(newId);
}

void UserController::recieveMessage(QByteArray data)
{
    using namespace based_dfs_struct;
    QList<QByteArray> list;
    list << "SERVIC"
         << "profile_email.user" << data << "actor";
    QByteArray res = Serialization::serialize(list, Serialization::INFORMATION_SEPARATOR_ONE);
    QFile fin(USER_DATA_FOLDER + '/' + "profile_email.user");
    fin.open(QIODevice::WriteOnly);
    // data.append('\n');
    fin.write(data);
    fin.flush();
    fin.close();
    //    if (this->currentActorId.getHexValue() == "")
    //    {
    //        QFile file("data.temp");
    //        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    //        file.write(res);
    //        file.flush();
    //        file.close();
    //    }
    //    else
    //        send(res);
}

void UserController::userRegistration(const QString &userId, const QVariantMap &map, int type)
{
    qDebug() << "UserController::modelRegistration" << userId << map;

    auto profileMap = loadProfile(userId);
    profileMap["type"] = type;
    profileMap["roleData"] = map;
    saveProfile(profileMap);
}

void UserController::receiveData(QByteArray data)
{
    // using namespace ui_messages;
    QList<QByteArray> list =
        Serialization::deserialize(data, Serialization::INFORMATION_SEPARATOR_ONE);
    if (list.size() == 4)
    {
        qDebug(logInfo()) << "void UserController::recieveData(QByteArray data)" << list.at(3)
                          << "---" << list.at(1);
    }
    else
    {
        qDebug() << "reciveData: userConroler " << data;
    }
    // TODO: обработчик в отдельные функции / классы
    auto type = list[0]; // TODO: service if user from 2 devices
    auto file = list[1];
    auto actor = list[3];

    qDebug() << ">" << type << file << actor;

    if (type == "servic")
    {
        if (file == "avatar")
            updateAvatarImage();
        if (file == "profile.dat" && actor == currentActorId)
            currentProfileUpdate(loadProfile(actor));
    }

    if (type == "postes")
        newsPage->loadPosts({});

    if (type == "events")
        eventModel->loadEvents("");
}
using namespace based_dfs_struct;
// void UserController::setActorIdslot(bool flag, BigNumber actorId)
//{
//    if (flag)
//    {
//        this->currentActorId = actorId;
//        QFile file(USER_DATA_FOLDER + '/' + "profile_email.user");
//        file.open(QIODevice::ReadOnly);
//        QList<QByteArray> result;
//        while (!file.atEnd())
//        {
//            QByteArray data = file.readLine();
//            QList<QByteArray> list =
//                Serialization::deserialize(data, Serialization::TX_PAIR_FIELD_SPLITTER);
//            list.append(actorId.toByteArray());
//            QByteArray ser =
//                Serialization::serialize(list, Serialization::TX_PAIR_FIELD_SPLITTER);
//            file.close();
//            result.append(ser);
//        }
//        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
//        for (auto &el : result)
//        {
//            file.write(el);
//            file.write("\n");
//        }
//    }
//}

void UserController::process()
{
    qDebug() << "[UserController] Hello, Thread";
}

void UserController::receivePublicKeyFromNeoteManagers(QByteArray PubKey)
{
    this->key = PubKey;
}

// slot for encrypt massege
void UserController::ecnryptMassage(QByteArray message, QByteArray rx_id, QByteArray tx_id)
{
    KeyPublic pubKey(this->key);
    qDebug() << "slot \"encrypt massege\" void UserController::ecnryptMassage(QByteArray "
                "message, "
                "QByteArray rx_id, QByteArray tx_id)";

    emit sendMessageToDFS(pubKey.encrypt(message), rx_id, tx_id);
}

void UserController::getPrKey(QByteArray keyPr)
{
    this->prKey = keyPr;
}

void UserController::decryptMessage(QList<QByteArray> message, QByteArray actorId)
{
    emit takePrKeyFrom(BigNumber(actorId));
    qDebug() << "void UserController::decryptMessage(QByteArray message, QString actorId):::::"
             << message << "-----" << actorId;
    KeyPrivate priKey(this->prKey);
    QStringList res;
    while (!message.isEmpty())
    {
        res.append(QString(priKey.decrypt(message.takeFirst())));
        qDebug() << "message" << res;
    }
    emit signalToQMLMessage(res, QString(actorId));
}

// slots

QList<Transaction> UserController::getTransactionsList(BigNumber actorId)
{
    // read block from file
    qDebug() << "signal has been connected";
    QFile fileTransaction("path.dat");
    QList<Transaction> returnList;
    fileTransaction.open(QIODevice::ReadOnly | QIODevice::Text);
    if (!fileTransaction.isOpen())
        qDebug() << "file with transaction has not been open";
    while (!fileTransaction.atEnd())
    {
        QByteArray transaction = fileTransaction.readLine();
        //        qDebug() << "transaction from file:" << transaction;
        Transaction tr(transaction);
        //        qDebug() << "Object Transaction class" << tr.toString()
        //                 << "tr.sender: " << tr.getSender().toString();

        if ((tr.getSender() == actorId) || (tr.getReceiver() == actorId))
            returnList.append(tr);
    }
    return returnList;
}

void UserController::loadPathToNews(int i)
{
    emit requestNewsPath(i, this->currentActorId);
}

void UserController::getCurrentActor(BigNumber actorId)
{
    this->currentActorId = actorId;
}

void UserController::setListNewsPath(QStringList list)
{
    qDebug() << "void UserController::setListNewsPath(QStringList "
                "list):__________--------_____-"
             << list;
    emit getPathToLastNews(list);
}

void UserController::retranslateDFSfromProfile(QByteArray data)
{
    qDebug() << "ActorId" << this->currentActorId;
    emit retranslateDFSfromProfileSignal(data, this->currentActorId);
}

void UserController::retranslateProfilefromDFS(QByteArray data, BigNumber actorId)
{
    emit retranslateProfilefromDFSSignal(data, actorId);
}

void UserController::retranslateMessageToReadFromDFS(QString actorId1, QString actorId2)
{
    emit readMessageFromFileInDFS(actorId1.toUtf8(), actorId2.toUtf8());
}

void UserController::recieve(QStringList list)
{
    qDebug() << "void UserController::recieve(QStringList list)::" << list;
    emit sendAllPathToProFile(list);
}

void UserController::requestAllPathProfile(QString actorId)
{
    qDebug() << "void UserController::requestAllPathProfile(QString actorId):" << actorId;
    emit requestAllProfilePath(BigNumber(actorId.toUtf8()));
}

void UserController::readAndDecreptMessageList(int count, QString path)
{
    Q_UNUSED(path)
    qDebug() << "-------------------------------\n-------------------------------\n------"
                "-------------------------\n"
             << "void UserController::readAndDecreptMessageList(int count, QString path)";
    //    QFile file(path);
    QFile file("..//data/1/SERVICE/chat_" + Serialization::serialize({ "1", "2" }, "_")
               + ".dat");
    file.open(QIODevice::ReadOnly);
    QList<QByteArray> list = Serialization::deserialize(
        file.readAll(), "\n" /*Serialization::INFORMATION_SEPARATOR_ONE*/);
    QList<ChatMessages> messageList;
    for (int i = list.length() - (count - 1) * 10 - 1; i > -1; i--)
        messageList.append(ChatMessages(list.at(list.length() - 1 - i)));
    qDebug() << "message list: " << list;
    emit messageListToChat(messageList);
}

void UserController::readNewMessage(QString fileName)
{
    QFile file(fileName);
    file.open(QIODevice::ReadOnly);
    QList<QByteArray> serializedList =
        Serialization::deserialize(file.readAll(), Serialization::INFORMATION_SEPARATOR_ONE);
    emit sendMessageToChat(ChatMessages(serializedList.last()));
}

void UserController::saveMessageInDFS(QByteArray message, QByteArray actor)
{
    emit sendMessageToSaveDFS(message, actor);
}

void UserController::saveMyMessageToDFS(QByteArray message, QByteArray actors)
{
    emit saveMessageMyMessageToDFS(message, actors);
}

void UserController::slotForLifePost(QStringList list)
{
    emit signalForLifePost(list);
}

void UserController::tempForImages(QVariantMap &map) // TODO
{
    QStringList images = map["images"].toStringList();
    BigNumber newImage = CardManager::getNameForNewFile(Type::images);

    for (QString &image : images)
    {
        QString originalImage = QUrl(image).toLocalFile();
        QFile file(originalImage);
        file.open(QFile::ReadOnly);
        imagesTempData = file.readAll();
        file.close();

        universalSender(ui_messages::page::images, based_dfs_struct::State::NEWSTATE, "");
        imagesTempData = "";

        image = newImage.toString();
        newImage++;
    }

    map["images"] = images;
}

ContractsModel *UserController::getContractsModel() const
{
    return contractsModel;
}

PortfolioModel *UserController::getPortfolioModel() const
{
    return portfolioModel;
}

EventsModel *UserController::getMyEventModel() const
{
    return myEventModel;
}

void UserController::addPost(QVariantMap post)
{
    qDebug() << "addPost: universalSender";
    tempForImages(post);
    newsPage->setChanges(QJsonDocument::fromVariant(post).toJson(QJsonDocument::Compact));
    universalSender(ui_messages::page::post, based_dfs_struct::State::NEWSTATE, "");

    newsPage->loadPosts({});
}

void UserController::addEvent(QVariantMap event)
{
    qDebug() << "addEvent: universalSender";
    tempForImages(event);
    eventModel->setChanges(QJsonDocument::fromVariant(event).toJson(QJsonDocument::Compact));
    universalSender(ui_messages::page::event, based_dfs_struct::State::NEWSTATE, "");
    DfsRequest temp(DFS_REQUESTS::POST_FILE_REQUEST, currentActorId,
                    currentActorId.toByteArray());
    emit tempToDfs(temp);
}

void UserController::subscribing(QByteArray userId)
{
    if (userId.isEmpty())
        return;

    auto profile = loadProfile(currentActorId.toByteArray());
    auto subs = profile["subscriptions"].toStringList();
    if (subs.indexOf(userId) == -1)
        subs.append(userId);
    else
        subs.removeAt(subs.indexOf(userId));
    qDebug() << subs;
    profile["subscriptions"] = subs;

    saveProfile(profile);
}

void UserController::addComment(QByteArray postId, QString body)
{
    QVariantMap sub = { { "userId", 2 }, { "date", 22222 }, { "text", "2text text text" } };
    QVariantMap comment = { { "userId", 1 },
                            { "date", 11111 },
                            { "text", "text text text" },
                            { "sub", QVariantList{ sub, sub, sub } } };

    qDebug() << QJsonDocument::fromVariant(comment).toJson(QJsonDocument::Compact);

    QByteArray serialized = Serialization::serialize(
        { "POSTES", postId + "c", body.toUtf8(), this->currentActorId.toByteArray() },
        Serialization::INFORMATION_SEPARATOR_ONE);
    send(serialized);
}

void UserController::addPortfolio(QString image)
{
    return;
    BigNumber newImage = CardManager::getLastSavedFile(this->currentActorId, Type::images);

    QString originalImage = QUrl(image).toLocalFile();
    qDebug() << image << originalImage;
    QFile file(image);
    file.open(QFile::ReadOnly);
    QByteArray data = file.readAll();
    file.close();

    QByteArray imageSerialization =
        Serialization::serialize({ "IMAGES", "new", data, this->currentActorId.toByteArray() },
                                 Serialization::INFORMATION_SEPARATOR_ONE);

    send(imageSerialization);

    newImage++;
    auto profile = loadProfile(currentActorId.toString());
    auto portfolio = profile["portfolio"].toStringList();
    portfolio << newImage.toString();
    profile["portfolio"] = portfolio;
    saveProfile(profile);
}

void UserController::setServerStatus(bool serverStatus)
{
    if (m_serverStatus == serverStatus)
        return;

    m_serverStatus = serverStatus;
    emit serverStatusChanged(m_serverStatus);
}

void UserController::setQmlEngine(QQmlApplicationEngine *engine)
{
    this->engine = engine;
}

void UserController::getProfile(QString userId, QJSValue func)
{
    auto profile = loadProfile(userId);
    QJSValue jsProfile = engine->toScriptValue(profile);
    func.call({ jsProfile });
}

// function not needed refactoring
void UserController::universalSender(ui_messages::page page, based_dfs_struct::State state,
                                     const QByteArray &data)
{
    qDebug() << "universalSender" << ui_messages::toString(page)
             << based_dfs_struct::toString(state);

    switch (page)
    {
    case ui_messages::registation:
    {
        ui_messages::uiType<WelcomePage> temp(page, currentActorId);
        QByteArray serialize = temp.serialized(welcomePage->getChanges(), state);
        emit send(serialize);
        return;
    }
    case ui_messages::images:
    case ui_messages::miniAva:
    {
        ui_messages::uiType<NewsModel> temp(page, currentActorId);
        qDebug() << "universalSender: post";
        QByteArray serialize = temp.serialized(imagesTempData, state);
        emit send(serialize);
        return;
    }
    case ui_messages::post:
    {
        ui_messages::uiType<NewsModel> temp(page, currentActorId);
        qDebug() << "universalSender: post";
        QByteArray serialize = temp.serialized(newsPage->getChanges(), state);
        emit send(serialize);
        return;
    }
    case ui_messages::profile:
    {
        // ui_messages::uiType<Profile> temp(page, currentActorId);
        // QByteArray serialize = temp.serialized(data, state);
        // emit send(serialize);
        return;
    }
    case ui_messages::event:
    {
        ui_messages::uiType<EventsModel> temp(page, currentActorId);
        qDebug() << "universalSender: events";
        QByteArray serialize = temp.serialized(eventModel->getChanges(), state);
        emit send(serialize);
        return;
    }
    case ui_messages::wallet:
    {
        // ui_messages::uiType<WalletController> temp(page, currentActorId);
        // QByteArray serialize = temp.serialized(welcomePage->getChanges(), state);
        // emit send(serialize);
        return;
    }
        //    case ui_messages::contract:
        //    {
        //        ui_messages::uiType<ContractsModel> temp(page, currentActorId);
        //        QByteArray serialize = temp.serialized(welcomePage->getChanges(), state);
        //        emit send(serialize);
        //        return;
        //    }
    }
}
// slots:
void UserController::setUserId(BigNumber userId)
{
    currentActorId = userId;
    universalSender(ui_messages::page::registation, based_dfs_struct::State::NEWSTATE, "");
    emit sendForEncryptingORDecrypting(crypting::SIG_IN_PAGE,
                                       crypting::ENCRYPT_USE_ACTOR_REQUEST,
                                       welcomePage->serializeUserData(), userId.toByteArray());
    emit regEnded(true);
}
void UserController::logIn()
{
    QFile *file = new QFile(Utils::USER_DATA_FILE_NAME);
    file->open(QIODevice::ReadOnly);
    QList<QByteArray> serialize =
        Serialization::deserialize(file->readAll(), Serialization::INFORMATION_SEPARATOR_ONE);
    if (serialize.size() == 2)
        emit sendForEncryptingORDecrypting(crypting::LOG_IN_PAGE,
                                           crypting::DECRYPT_USE_ACTOR__REQUEST,
                                           serialize.at(1), serialize.at(0));
    else
        qDebug() << "list out of range";
    file->flush();
    file->close();
    delete file;
    //    QList<QByteArray> accSerialize =
    //        Serialization::deserialize(file->readAll(),
    //        Serialization::INFORMATION_SEPARATOR_ONE);
    //    for (QByteArray el : accSerialize)
    //    {
    //        QByteArray hash = Utils::calcKeccak(welcomePage->serializeUserData());

    //    }
}
void UserController::avtoLogIn(QByteArray hash)
{
    QFile *file = new QFile(Utils::USER_DATA_FILE_NAME);
    file->open(QIODevice::ReadOnly);
    QList<QByteArray> serialize =
        Serialization::deserialize(file->readAll(), Serialization::INFORMATION_SEPARATOR_ONE);
    if (serialize.size() == 2)
        emit sendForEncryptingORDecrypting(crypting::LOG_IN_PAGE,
                                           crypting::DECRYPT_USE_ACTOR__REQUEST,
                                           serialize.at(1), serialize.at(0));
    else
        qDebug() << "list out of range";
    file->flush();
    file->close();
    delete file;
}
void UserController::recieveEncryptOrDecryptData(int place, int response, QByteArray data)
{
    QByteArray encryptData = "";
    QByteArray decryptData = "";
    switch (response)
    {
    case crypting::DECRYPT_DATA_RESPONSE:
    {
        decryptData = data;
        break;
    }
    case crypting::ENCRYPT_DATA_RESPONSE:
    {
        encryptData = data;
        break;
    }
    }
    switch (place)
    {
    case crypting::LOG_IN_PAGE:
    {
        bool succed = (data == welcomePage->serializeUserData());
        if (succed)
        {
            QFile *file = new QFile(Utils::USER_DATA_FILE_NAME);
            file->open(QIODevice::ReadOnly);
            QList<QByteArray> serialize = Serialization::deserialize(
                file->readAll(), Serialization::INFORMATION_SEPARATOR_ONE);
            if (serialize.size() == 2)
                currentActorId = BigNumber(serialize.at(0));
            else
                qDebug() << "list out of range";
            emit initDfs();
            file->close();
            delete file;
        }
        qDebug() << "Uraaa" << data;

        emit regEnded(succed);
        break;
    }
    case crypting::SIG_IN_PAGE:
    {
        QByteArray serialize =
            Serialization::serialize({ currentActorId.toByteArray(), encryptData },
                                     Serialization::INFORMATION_SEPARATOR_ONE);
        welcomePage->savedUserData(serialize);
        break;
    }
    }
}
void UserController::profileRecieve(QString userId, QByteArray data)
{
}
