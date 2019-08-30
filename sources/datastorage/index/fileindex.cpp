#include "datastorage/index/fileindex.h"

FileIndex::FileIndex(const QString &folderName, int sectionSize)
{
    this->folderName = folderName;
    this->sectionSize = sectionSize;
    firstSavedId = loadFirstId();
    lastSavedId = loadLastId();
    QDir dir(DataStorage::BLOCKCHAIN_INDEX + '/' + folderName);
    QFileInfoList sectionList =
        dir.entryInfoList(QDir::Filter::Dirs | QDir::Filter::NoDot | QDir::Filter::NoDotDot);
    int count = 0;
    for (auto &el : sectionList)
    {

        QFileInfoList files =
            el.dir().entryInfoList(QDir::Filter::Dirs | QDir::Filter::NoDot | QDir::Filter::NoDotDot);
        count += files.size();
    }
    records = count;
}

BigNumber FileIndex::loadFileFromSection(std::function<QString(const QStringList &folders)> getFolder,
                                         std::function<QString(const QStringList &files)> getFile)
{
    auto asBigNumComparator = [](const QString &file1, const QString &file2) {
        return BigNumber(file1.toLocal8Bit()) < BigNumber(file2.toLocal8Bit());
    };

    QDir folder(getFolderPath());

    // sections
    qDebug() << "FILE INDEX: "
             << "loadFileFromSection(): " << folder.path();
    QStringList list = folder.entryList(QDir::Filter::Dirs | QDir::Filter::NoDotAndDotDot);
    if (list.isEmpty())
    {
        qDebug() << "FILE INDEX: "
                 << "loadFileFromSection(): "
                 << "folder.entryList: empty";
        return BigNumber();
    }
    std::sort(list.begin(), list.end(), asBigNumComparator);
    folder.cd(getFolder(list)); // go to section

    // files in sections
    qDebug() << "FILE INDEX: "
             << "loadFileFromSection(): " << folder.path();
    list = folder.entryList(QDir::Filter::Files | QDir::Filter::NoDotAndDotDot);
    if (list.isEmpty())
    {
        qDebug() << "FILE INDEX: "
                 << "loadFileFromSection(): "
                 << "folder.entryList->folder.entryList: empty";
        return BigNumber();
    }
    std::sort(list.begin(), list.end(), asBigNumComparator);

    qDebug() << "FILE INDEX: "
             << "loadFileFromSection(): lastId - "
             << (list.isEmpty() ? BigNumber() : BigNumber(getFile(list).toLocal8Bit()));
    return list.isEmpty() ? BigNumber() : BigNumber(getFile(list).toLocal8Bit());
}

BigNumber FileIndex::loadFirstId()
{
    BigNumber firstSavedId = loadFileFromSection([](const QStringList &folders) { return folders[0]; },
                                                 [](const QStringList &files) { return files[0]; });

    if (!firstSavedId.isEmpty())
    {
        qDebug() << "FIFE INDEX: loadFirsId: Loaded first saved id:" << firstSavedId;
    }
    else
    {
        qDebug() << "FIFE INDEX: loadFirsId: First saved id is not loaded";
    }

    return firstSavedId;
}

BigNumber FileIndex::loadLastId()
{
    BigNumber lastSavedId = loadFileFromSection([](const QStringList &folders) { return folders.last(); },
                                                [](const QStringList &files) { return files.last(); });

    if (!lastSavedId.isEmpty())
    {
        qDebug() << "Loaded last saved id:" << lastSavedId;
    }
    else
    {
        qDebug() << "Last saved id is not loaded";
    }

    return lastSavedId;
}

void FileIndex::removeAll()
{
    QString folderPath = this->getFolderPath();
    qDebug() << "Clearing file index: " << folderPath;

    QDir folder(folderPath);
    for (const QString &section :
         folder.entryList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot, QDir::SortFlag::Name))
    {
        QDir dir(folderPath + QString("/") + section);
        dir.removeRecursively();
    }

    // update state
    this->records = 0;
    this->firstSavedId = 0;
    this->lastSavedId = 0;
}

BigNumber FileIndex::calcSection(BigNumber id) const
{
    return id / BigNumber(sectionSize);
}

BigNumber FileIndex::getFirstSection() const
{
    return calcSection(this->firstSavedId);
}

BigNumber FileIndex::getLastSection() const
{
    return calcSection(this->lastSavedId);
}

int FileIndex::removeById(const BigNumber &id)
{
    qDebug() << "Removing record with id" << id.toString();
    if (id < firstSavedId)
    {
        removeAll();
    }
    qDebug() << lastSavedId << "(last saved id)" << id << "(id to remove)";

    BigNumber currentIdToRemove = id;

    while (currentIdToRemove <= lastSavedId)
    {
        QString pathToFile = buildFilePath(currentIdToRemove);
        qDebug() << "To remove: " << pathToFile;
        QFile file(pathToFile);
        if (file.exists() && !file.isOpen())
        {
            bool isRemoved = file.remove();
            if (isRemoved)
            {
                this->records--;
            }
        }
        currentIdToRemove++;
    }

    this->lastSavedId = BigNumber(id) - 1;
    return 0;
}

int FileIndex::removeFromEnd(const BigNumber &count)
{
    if (count <= 0)
    {
        // input should be more then 0
        return -1;
    }

    if (records < count)
    {
        // excection: there no enough records to remove
        return -2;
    }

    BigNumber idToRemove = this->lastSavedId - count + 1;
    return this->removeById(idToRemove);
}

QString FileIndex::buildFilePath(const BigNumber &id) const
{
    BigNumber section = this->calcSection(id);
    QString pathToFolder = getFolderPath() + "/" + section.toString();

    QDir dir(pathToFolder);
    if (!dir.exists())
    {
        qDebug() << "Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + id.toString();
}

QString FileIndex::getFolderName() const
{
    return this->folderName;
}

QString FileIndex::getFolderPath() const
{
    return DataStorage::BLOCKCHAIN_INDEX + "/" + this->getFolderName();
}

bool FileIndex::hasRecordLimit() const
{
    return !this->recordsLimit.isEmpty();
}

bool FileIndex::recordLimitIsReached() const
{
    return this->hasRecordLimit() && (this->records >= this->recordsLimit);
}

QByteArray FileIndex::getById(const BigNumber &id) const
{
    QString path = buildFilePath(id);
    QFile file(path);

    if (!file.exists())
    {
        qDebug() << "Can't get the file" << path << "(File is not exits)";
        return QByteArray();
    }

    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray data;
        QDataStream stream(&file);
        stream >> data;
        file.close();
        return data;
    }

    qDebug() << "Can't get the file:" << path << "(File is not opened)";
    return QByteArray();
}

int FileIndex::add(const BigNumber &id, const QByteArray &data)
{
    QString path = buildFilePath(id);
    QFile file(path);

    qDebug() << "Saving the file:" << path;

    if (file.exists())
    {
        qDebug() << "Can't save the file" << path << "(File already exits)";
        return Errors::FILE_ALREADY_EXISTS;
    }

    if (recordLimitIsReached())
    {
        this->removeById(this->getFirstSavedId());
        this->firstSavedId++; // todo: check!
    }

    if (file.open(QIODevice::WriteOnly))
    {
        QDataStream stream(&file);
        stream << data;
        file.flush();
        file.close();

        this->records = records + 1;

        // updating last saved id is a regular operation
        if (id > this->lastSavedId)
        {
            this->lastSavedId = id;
        }

        // but updating the first saved id is rarely (should be logged)
        if (id < this->firstSavedId || firstSavedId.isEmpty())
        {
            qDebug() << "First saved id is updated from" << firstSavedId << "to" << id;
            this->firstSavedId = id;
        }

        return 0;
    }

    qCritical() << "Can't save the file" << path << "(File is not opened)";
    return Errors::FILE_IS_NOT_OPENED;
}

BigNumber FileIndex::getFirstSavedId() const
{
    return this->firstSavedId;
}

BigNumber FileIndex::getLastSavedId() const
{
    return this->lastSavedId;
}

BigNumber FileIndex::getRecords() const
{
    return this->records;
}
