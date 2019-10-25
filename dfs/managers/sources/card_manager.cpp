#include "dfs/managers/headers/card_manager.h"

QList<QByteArray> CardManager::sorting(QList<QByteArray> list)
{

    QMap<QByteArray, QByteArray> sorted_data;
    QList<QByteArray> res;

    for (auto &i : list)
    {
        sorted_data[Serialization::deserialize(i, Serialization::INFORMATION_SEPARATOR_TWO).at(2)] = i;
    }
    QMapIterator<QByteArray, QByteArray> i(sorted_data);

    while (i.hasNext())
    {
        i.next();
        res.append(
            Serialization::deserialize(i.value(), Serialization::INFORMATION_SEPARATOR_TWO).takeFirst());
    }
    return res;
}

BigNumber CardManager::getLastSavedFile(const BigNumber &actorId, const based_dfs_struct::Type type)
{
    QString cardPath = "";
    if (type == based_dfs_struct::postes)
        cardPath = based_dfs_struct::POST_CARD_FILE_NAME;
    else if (type == based_dfs_struct::events)
        cardPath = based_dfs_struct::EVENT_CARD_FILE_NAME;
    else if (type == based_dfs_struct::images)
        cardPath = based_dfs_struct::IMAGE_CARD_FILE_NAME;
    QString path =
        Serialization::serialize({ based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8(), actorId.toActorId() }, '/')
        + toByteArray(type) + cardPath;
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    QByteArray line = file.readLine();
    if (line.isEmpty())
        line = "-1";
    BigNumber res = BigNumber(line);
    //    res++;
    file.close();
    return res;
}

QList<QByteArray> CardManager::getMyNew() //   +-
{
    //вичитує з карти файла який відповідає за пости всі пости, з кожної
    //карти в кожному загруженому до нас екторі і сортує їх по даті

    QList<QByteArray> line, myPosts;
    QFile file(based_dfs_struct::cardFileConnections[based_dfs_struct::postes]); // check
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    while (!file.atEnd())
        line.append(file.readLine());
    file.close();
    myPosts = sorting(line);
    return myPosts;
}

QList<QByteArray> CardManager::getPosts(const BigNumber &userId) // +- ???
{
    //вичитати пости с карт файла заданого ектора вам потрібна карта файлу цього екторв
    QFile file(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
               + toString(based_dfs_struct::postes) + based_dfs_struct::POST_CARD_FILE_NAME);
    QList<QByteArray> posts;
    if (!file.exists())
        qDebug() << "file not exist" << file;
    file.flush();
    file.close();

    bool fileStatus = file.open(QIODevice::ReadOnly);
    QByteArray infoList = file.readAll();

    QList<QByteArray> list =
        Serialization::deserialize(infoList, Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER);
    if (!list.isEmpty())
    {
        list.removeFirst();
        for (QByteArray el : list)
        {
            QList<QByteArray> postesPath =
                Serialization::deserialize(el, Serialization::DFS_DFSTRUCT_DELIMETR);
            posts.append(postesPath.at(3));
        }
    }
    file.close();
    return list;
}

QStringList CardManager::getAll(based_dfs_struct::Type type)
{
    QStringList all;

    const QStringList allUserIds =
        QDir(based_dfs_struct::ROOT_FOOLDER_NAME).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (auto &userId : allUserIds)
    {
        QStringList files = getFilesByType(userId.toLatin1(), type);
        all << files;
    }

    return all;
}

QStringList CardManager::getForUser(based_dfs_struct::Type type, QString userId)
{
    /*
    QList<QByteArray> all;
    QString fileName =
        based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + toString(type) + cardFileName;
    QFile file(fileName);

    // qDebug() << "fileName" << fileName;

    if (!file.open(QIODevice::ReadOnly))
        return {};

    auto cardFileAll = file.readAll();
    if (cardFileAll.isEmpty())
        return {};
    // qDebug() << "cardFileAll" << cardFileAll;
    auto cardFileContent = cardFileAll.split('=');
    // qDebug() << "cardFileContent" << cardFileContent << cardFileContent.length();
    if (cardFileContent.length() < 3)
        return {};

    for (int i = 1; i != cardFileContent.length() - 1; ++i)
    {
        auto line = QString(cardFileContent[i]).split("**");
        // qDebug() << "Line" << line;
        if (line.length() < 3)
            continue;
        auto filePath = line[3].toUtf8();
        all.append(filePath);
    }

    file.close();

    return all;
    */
    return getFilesByType(userId.toLatin1(), type);
}

QList<QByteArray> CardManager::getMyEvents() // do i need to sort events?
{
    //вичитати всі івенти з моєї карти івентів

    QList<QByteArray> line, myEvents;
    QFile file(based_dfs_struct::cardFileConnections[based_dfs_struct::events]); // check
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    while (!file.atEnd())
        line.append(file.readLine());

    myEvents = sorting(line);
    file.close();
    return myEvents;
}

QList<QByteArray> CardManager::getEvents(const BigNumber &userId)
{
    return {};
}

// QList<QByteArray> CardManager::getEvents(const BigNumber &userId) // copied from getPosts
//{
//    //вичитати всі івенти з карти заданого ектора
//    QFile file(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
//               + toString(based_dfs_struct::events) +
//               based_dfs_struct::EVENT_CARD_FILE_NAME);
//    QList<QByteArray> events;
//    file.open(QIODevice::ReadOnly | QIODevice::Text);
//    while (!file.atEnd())
//    {
//        DfsItem item(file.readLine());
//        events.append(item.getPath());
//    }
//    file.close();
//    return events;
//}
QList<QByteArray> CardManager::getAllMyChat()
{
    //вичитати всю карту чатів

    QList<QByteArray> line, myChats;
    QFile file(based_dfs_struct::cardFileConnections[based_dfs_struct::chates]);
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    while (!file.atEnd())
        line.append(file.readLine());
    file.close();
    myChats = sorting(line);
    return myChats;
}

QByteArray CardManager::getProfileById(const BigNumber &userId)
{
    QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
        + based_dfs_struct::toString(based_dfs_struct::Type::servic)
        + based_dfs_struct::SERVICE_CARD_FILE_NAME;
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    QList<QByteArray> list =
        Serialization::deserialize(file.readAll(), Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER);
    QByteArray data = "";
    if (!list.isEmpty())
    {
        data = list.at(1);
    }
    else
    {
        qDebug() << "erorr in CardManger";
        data = "erorr";
    }
    return data;
}

BigNumber CardManager::getNameForNewFile(based_dfs_struct::Type type)
{
    /*
     * Need add changes if CardFile will be so big
     * read file by char
     * and control memmory
     * function for find in big file
     */
    QFile *cardFile = new QFile(based_dfs_struct::cardFileConnections[type]);
    cardFile->open(QIODevice::ReadOnly);
    QByteArray cardFileData = cardFile->readAll();
    BigNumber amout = cardFileData.isEmpty()
        ? BigNumber("-1")
        : BigNumber(
            cardFileData.mid(0, cardFileData.indexOf(Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER)));
    amout++;
    cardFile->close();
    delete cardFile;
    return amout;
}

QString CardManager::getFileByName(const based_dfs_struct::Type type, const QByteArray &name)
{
    QFile file(based_dfs_struct::cardFileConnections[type]);
    QByteArray line;
    while ((!file.atEnd()))
    {
        if (line.isEmpty())
        {
            qDebug() << "empty line";
        }
        else
        {
            QList<QByteArray> list = Serialization::deserialize(line, Serialization::TX_FIELD_SPLITTER);
            if (list.size() == 10)
            {
                if (list.at(0) == name)
                    return list.at(3);
            }
            else
                qDebug() << list << "cardManager";
        }
    }
    file.close();
    return "not found";
}

void CardManager::appendToCard(based_dfs_struct::Type type, const QByteArray &serialize,
                               const BigNumber &userId)
{
    /*
     * Need add changes if CardFile will be so big
     * read file by char
     * and control memmory
     * function for find in big file
     * for usual cardfile use DFS_CARD_FILE_UNIVERSAL_DELIMITER
     * rootCard file use DFS_ROOT_CARD_FILE_SECTION_DELIMITER
     */

    QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
        + based_dfs_struct::toString(type) + based_dfs_struct::typeCardFilesMap[type];

    QFile *file = new QFile(path);

    if (!file->exists())
        file->open(QIODevice::WriteOnly | QIODevice::Truncate);
    file->open(QIODevice::ReadOnly);
    QByteArray dataFile = file->readAll();
    QList<QByteArray> list =
        Serialization::deserialize(dataFile, Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER);
    if (list.isEmpty())
        list.append("-1");

    BigNumber amount = BigNumber(list.at(0));
    file->remove();
    file->flush();
    file->close();
    file->open(QIODevice::WriteOnly);
    amount++;
    list[0] = amount.toByteArray();
    list.append(serialize);
    qDebug() << file->fileName().toUtf8()
             << Serialization::serialize(list, Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER);
    QByteArray line = Serialization::serialize(list, Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER);

    file->write(line);
    file->flush();
    file->close();
    delete file;
    QFile *cardFile = new QFile(based_dfs_struct::ROOT_CARD_FILE_NAME);
    cardFile->open(QIODevice::ReadOnly);
    QByteArray data = cardFile->readAll();
    QList<QByteArray> cardList =
        Serialization::deserialize(data, Serialization::DFS_ROOT_CARD_FILE_SECTION_DELIMITER);
    QList<QByteArray> savedCardsPath = {};
    for (QByteArray el : cardList)
        savedCardsPath << Serialization::deserialize(el, Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    cardFile->close();
    if (int indexOfCardFile = savedCardsPath.indexOf(path.toUtf8()) == -1)
    {
        cardFile->open(QIODevice::WriteOnly | QIODevice::Append);
        QByteArray rootLine =
            Serialization::serialize({ amount.toByteArray(), Utils::calcKeccak(line), path.toUtf8() },
                                     Serialization::DFS_ROOT_CARD_FILE_DELIMITER)
            + Serialization::DFS_ROOT_CARD_FILE_SECTION_DELIMITER;
        cardFile->write(rootLine);
        cardFile->flush();
        cardFile->close();
    }
    else
    {
        cardList[indexOfCardFile] =
            Serialization::serialize({ amount.toByteArray(), Utils::calcKeccak(line), path.toUtf8() },
                                     Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
        //        if (cardFile->size() < (1024 * 1024 * 10))
        //        {
        cardFile->open(QIODevice::WriteOnly | QIODevice::Truncate);
        cardFile->write(
            Serialization::serialize(cardList, Serialization::DFS_ROOT_CARD_FILE_SECTION_DELIMITER));
        cardFile->flush();
        cardFile->close();
    }
    delete cardFile;
}

void CardManager::createdAllCards(const BigNumber &userId)
{
    for (auto &el : based_dfs_struct::typeCardFilesMap)
    {
        QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
            + based_dfs_struct::toString(el.first) + el.second;
        QFile file(path);
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        file.flush();
        file.close();
    }
}

int CardManager::checkDfsState(const BigNumber &userId) // not at
{
    //    QList<QString> list = getAllFiles(userId);
    if (!QDir(based_dfs_struct::ROOT_FOOLDER_NAME).exists())
        return -1;
    else
        return 0;
}

void CardManager::createdAllConnections()
{
    QFileInfoList list = QDir(based_dfs_struct::ROOT_FOOLDER_NAME).entryInfoList();
    for (auto &el : list)
        if ((el.fileName() == ".") || (el.fileName() == "..")
            || (el.path() == based_dfs_struct::ROOT_CARD_FILE_NAME))
            list.removeOne(el);
    for (auto &el : list)
    {
        QMap<based_dfs_struct::Type, QString> cardConnections = {};
        for (based_dfs_struct::Type elType : based_dfs_struct::typesVec)
            cardConnections[elType] = QString(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + el.fileName() + '/'
                                              + based_dfs_struct::toString(elType) + '/'
                                              + based_dfs_struct::typeCardFilesMap[elType]);
        DFS_ERRORS::allDfsCardFileConnections[el.fileName()] = cardConnections;
    }
}

QMap<based_dfs_struct::Type, QByteArray> CardManager::getCardHashFromRoot(const BigNumber &userId)
{
    /*
     * needed to modern for bigs file
     * Use RAM
     */
    QFile *file = new QFile(based_dfs_struct::ROOT_CARD_FILE_NAME);
    file->open(QIODevice::ReadOnly);
    QList<QByteArray> cardFilesList =
        Serialization::deserialize(file->readAll(), Serialization::DFS_ROOT_CARD_FILE_SECTION_DELIMITER);
    QMap<based_dfs_struct::Type, QByteArray> result = {};
    for (QByteArray el : cardFilesList)
    {
        QList<QByteArray> elementOfCards =
            Serialization::deserialize(el, Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
        QList<QByteArray> desirializePath = Serialization::deserialize(elementOfCards.at(2), "/");
        if (userId.toActorId() == desirializePath.at(1))
            result[based_dfs_struct::convertToDFType(desirializePath.at(2))] = elementOfCards.at(1);
    }
    file->close();
    delete file;
    return result;
    //    while (!file->atEnd())
    //    {
    //        QByteArray line = file->readLine();
    //        if (line.at(0) == '-')
    //            if (BigNumber(line.remove(0, 1)) == userId)
    //            {
    //                QMap<based_dfs_struct::Type, QByteArray> res;
    //                for (int i = 0; i < 7; i++)
    //                {
    //                    QList<QByteArray> list = Serialization::deserialize(
    //                        file->readLine(), Serialization::TX_FIELD_SPLITTER);
    //                    res[based_dfs_struct::convertToDFType(list.at(0))] = list.at(1);
    //                }
    //                return res;
    //            }
    //    }
    return {};
}

int CardManager::createdCardFilesConnection(const BigNumber &userId)
{
    QList<QByteArray> list = {};
    for (auto &el : based_dfs_struct::typesVec)
    {
        QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
            + based_dfs_struct::toString(el) + based_dfs_struct::typeCardFilesMap[el];
        if (!QFile(path).exists())
        {
            QFile(path).open(QIODevice::WriteOnly);
            QFile(path).close();
        }
        based_dfs_struct::cardFileConnections[el] = path;
    }
    return 0;
}

QList<QString> CardManager::getAllFiles(const BigNumber &userId)
{
    QList<QString> list = {};
    QList<QString> cardList = {};
    for (auto &el : based_dfs_struct::typesVec)
    {
        QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
            + based_dfs_struct::toString(el) + based_dfs_struct::typeCardFilesMap[el];
        if (QFile(path).exists())
        {
            QFile f(path);
            f.open(QIODevice::ReadOnly);
            QByteArray data = f.readAll();
            if (!data.isEmpty())
                cardList.append(path);
        }
    }
    for (QString &el : cardList)
    {
        QFile file(el);
        file.open(QIODevice::ReadOnly);
        QByteArray data = file.readAll();

        QList<QByteArray> rlist =
            Serialization::deserialize(data, Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER);
        qDebug() << rlist.takeFirst();
        for (QByteArray &el : rlist)
        {
            based_dfs_struct::DfStruct dfsFile(el);
            list.append(dfsFile.getPath());
        }
        file.close();
    }
    return list;
}

std::vector<std::pair<std::string, std::string>> CardManager::getAllFileWithHash(const BigNumber &userId)
{
    std::vector<std::pair<std::string, std::string>> result;
    QList<QString> cardList = {};
    for (auto &el : based_dfs_struct::typesVec)
    {
        QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
            + based_dfs_struct::toString(el) + based_dfs_struct::typeCardFilesMap[el];
        if (QFile(path).exists())
        {
            QFile f(path);
            f.open(QIODevice::ReadOnly);
            QByteArray data = f.readAll();
            if (!data.isEmpty())
                cardList.append(path);
        }
    }
    for (QString &el : cardList)
    {
        QFile file(el);
        file.open(QIODevice::ReadOnly);
        QByteArray data = file.readAll();

        QList<QByteArray> rlist =
            Serialization::deserialize(data, Serialization::DFS_CARD_FILE_UNIVERSAL_DELIMITER);
        qDebug() << rlist.takeFirst();
        for (QByteArray &el : rlist)
        {
            based_dfs_struct::DfStruct dfsFile(el);
            result.push_back(
                std::make_pair(dfsFile.getHash().toStdString(), dfsFile.getPath().toStdString()));
        }
        file.close();
    }
    return result;
}

QStringList CardManager::existsProfileFiles()
{
    QDir dir(based_dfs_struct::ROOT_FOOLDER_NAME);

    QStringList users = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    users.removeAt(users.indexOf("user"));

    for (int i = 0; i != users.length(); ++i)
    {
        QString tempUser =
            based_dfs_struct::ROOT_FOOLDER_NAME + "/" + users[i] + "/servic/profile.dat"; // TODO

        if (!QFile::exists(tempUser))
            users.removeAt(users.indexOf(users[i--]));
        else
            users[i] = tempUser;
    }

    return users;
}

QList<QByteArray> CardManager::getUserPosts(BigNumber userdId)
{
    return QList<QByteArray>();
}

QList<QByteArray> CardManager::getEventsTemp(BigNumber userId)
{
    return QList<QByteArray>();
}

QStringList CardManager::getImagesFromJson(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).toVariant().toMap()["images"].toStringList();
}

QStringList CardManager::getAllNotEmptyCardFile(const BigNumber &userId)
{
    QStringList cardList = {};
    for (auto &el : based_dfs_struct::typesVec)
    {
        QString path = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
            + based_dfs_struct::toString(el) + based_dfs_struct::typeCardFilesMap[el];
        if (QFile(path).exists())
        {
            QFile f(path);
            f.open(QIODevice::ReadOnly);
            QByteArray data = f.readAll();
            if (!data.isEmpty())
                cardList.append(path);
        }
    }
    return cardList;
}

QStringList CardManager::getFilesByType(const QByteArray &userId, based_dfs_struct::Type &type)
{
    QFile card(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + based_dfs_struct::ACTOR_CARD_FILE);
    card.open(QIODevice::ReadOnly);
    QList<QByteArray> list =
        Serialization::deserialize(card.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    if (list.isEmpty())
        return QStringList();
    QStringList result;
    for (const QByteArray &el : list)
    {
        QByteArray dType =
            Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(3);
        if (based_dfs_struct::toByteArray(type) == dType)
            result << "data/" + userId + "/"
                    + Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2);
    }
    return result;
}

QByteArray CardManager::getLastFileName(const QByteArray &userId)
{
    QFile file(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + based_dfs_struct::ACTOR_CARD_FILE);
    file.open(QIODevice::ReadOnly);

    QList<QByteArray> list =
        Serialization::deserialize(file.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    if (list.isEmpty())
        return "1";
    return Serialization::deserialize(list.takeLast(), Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2);
}

QStringList CardManager::getAllFiles(const QByteArray &userId)
{
    QFile card(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + based_dfs_struct::ACTOR_CARD_FILE);
    card.open(QIODevice::ReadOnly);
    QList<QByteArray> list =
        Serialization::deserialize(card.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    if (list.isEmpty())
        return QStringList();
    QStringList result;
    for (const QByteArray &el : list)

        result << Serialization::deserialize(el, Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2);

    return result;
}
