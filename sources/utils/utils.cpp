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

#include "utils/utils.h"

#include <QMimeDatabase>
#include <QStandardPaths>

template <typename T>

std::string to_string(T value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

QByteArray Utils::calcKeccak(const QByteArray &b)
{
    Keccak keccak;
    QByteArray hashmsg = keccak(b);
    return hashmsg;
}

// SERIALIZATION //

std::vector<std::string> Utils::split(const std::string &s, char c)
{
    auto end = s.cend();
    auto start = end;

    std::vector<std::string> v;
    for (auto it = s.cbegin(); it != end; ++it)
    {
        if (*it != c)
        {
            if (start == end)
                start = it;
            continue;
        }
        if (start != end)
        {
            v.emplace_back(start, it);
            start = end;
        }
    }
    if (start != end)
        v.emplace_back(start, end);
    return v;
}

QString KeyStore::makeKeyFileName(QString name)
{
    return name + KEY_TYPE;
}

void FileSystem::createFolderIfNotExist(QString path)
{
    QDir dir(path);
    if (!dir.exists())
    {
        dir = QDir();
        dir.mkpath(path);
    }
}

int Utils::compare(const QByteArray &one, const QByteArray &two)
{
    if (one.size() > two.size())
    {
        return one.size() - two.size();
    }
    else if (one.size() == two.size())
    {
        return static_cast<int>(one == two);
    }
    else
        return two.size() - one.size();
}

QByteArray Utils::getParam(const QString &param, const QByteArray &jsonDocument)
{
    QJsonDocument doc = QJsonDocument::fromBinaryData(jsonDocument);
    return doc.object().value(param).toString().toLocal8Bit();
}

bool FileSystem::tryToOpen(QFile &file, QIODevice::OpenMode mode)
{
    if (!file.open(mode))
    {
        qCritical().noquote() << QString("[WARNING] Can't open [%1] file").arg(file.fileName());
        return false;
    }
    return true;
}

QByteArray storedSpace::toByteArray(storedSpace::State state)
{
    if (state == storedSpace::State::NEWSTATE)
        return "NEWSTATE";
    if (state == storedSpace::State::CHANGEDS)
        return "CHANGEDS";
    if (state == storedSpace::State::DELSTATE)
        return "DELSTATE";
    return "UNRECOGS";
}

QString storedSpace::toString(storedSpace::State state)
{
    if (state == storedSpace::State::NEWSTATE)
        return "NEWSTATE";
    if (state == storedSpace::State::CHANGEDS)
        return "CHANGEDS";
    if (state == storedSpace::State::DELSTATE)
        return "DELSTATE";
    return "UNRECOGS";
}

storedSpace::State storedSpace::convertToDFSstate(QByteArray state)
{
    if (state == "NEWSTATE")
        return storedSpace::State::NEWSTATE;
    if (state == "CHANGEDS")
        return storedSpace::State::CHANGEDS;
    if (state == "DELSTATE")
        return storedSpace::State::DELSTATE;
    return storedSpace::State::UNRECOGS;
}

QByteArray Utils::intToByteArray(const int &number, const int &size)
{
    QByteArray num = QByteArray::number(number);
    QByteArray res = "";
    if (num.size() < size)
        for (int i = 0; i < size - num.size(); i++)
            res += "0";
    res += num;
    return res;
}

int Utils::qByteArrayToInt(const QByteArray &number)
{
    QByteArray num = "";
    int i = 0;
    //    bool flag = false;
    while (i < number.size())
    {
        if (number[i] == '0')
            i++;
        else
            break;
    }
    while (i < number.size())
    {
        num += number[i];
        i++;
    }
    int res = num.toInt();
    return res;
}

QByteArray Utils::calcKeccakForFile(const QString &path)
{
    QFile file(path);
    if (!file.exists())
    {
        qDebug() << "Utils::File not exist" << path;
        return "";
    }
    file.open(QIODevice::ReadOnly);
    long long _file_size = file.size();
    long long _data_offset = DataStorage::DATA_OFFSET;
    long long pos = 0;
    QList<QByteArray> hashList = {};
    while ((pos += _data_offset) < _file_size)
    {
        char *ch = new char[_data_offset];
        file.read(ch, _data_offset);
        hashList.append(Utils::calcKeccak(QByteArray(ch, _data_offset)));
        delete[] ch;
    }
    hashList.append(Utils::calcKeccak(file.read(_file_size - pos)));
    QByteArray hash;
    for (QByteArray &el : hashList)
        hash += el;
    file.close();
    return Utils::calcKeccak(hash);
}

bool Utils::encryptFile(const QString &originalName, const QString &encryptName, const QByteArray &key,
                        int blockSize)
{
    QFile orig(originalName);
    if (!orig.exists())
        return false;
    QFile encrypt(encryptName);
    bool origOpen = orig.open(QFile::ReadOnly);
    bool encryptOpen = encrypt.open(QFile::WriteOnly);
    if (!origOpen || !encryptOpen)
    {
        qDebug() << "[Utils::encryptFile] Error while loading files" << origOpen << encryptOpen;
        return false;
    }

    while (!orig.atEnd())
    {
        QByteArray part = orig.read(blockSize);
        QByteArray encrypted = BlowFish::encrypt(part, key);
        encrypt.write(encrypted);
        // qDebug() << "encrypted" << part.size() << encrypted.size();
    }

    qDebug() << "[DFS] Encrypted file" << originalName << "to" << encryptName << "with sizes" << orig.size()
             << encrypt.size();
    orig.close();
    encrypt.close();
    return QFile::exists(encryptName);
}

bool Utils::decryptFile(const QString &encryptName, const QString &decryptName, const QByteArray &key,
                        int blockSize) //
{
    blockSize = (blockSize / 8 + 1) * 8;
    QFile encrypt(encryptName);
    if (!encrypt.exists())
        return false;
    QFile decrypt(decryptName);

    bool encryptOpen = encrypt.open(QFile::ReadOnly);
    bool decryptOpen = decrypt.open(QFile::WriteOnly);
    if (!encryptOpen || !decryptOpen)
    {
        qDebug() << "[Utils::encryptFile] Error while loading files" << encryptOpen << decryptOpen;
        return false;
    }

    while (!encrypt.atEnd())
    {
        QByteArray part = encrypt.read(blockSize);
        QByteArray decrypted = BlowFish::decrypt(part, key);
        decrypt.write(decrypted);
        qDebug() << "decrypted" << part.size() << decrypted.size();
    }

    // qDebug() << "[DFS] Encrypted file" << originalName << "to" << encryptName << "with sizes" <<
    // orig.size()
    //    << encrypt.size();
    encrypt.close();
    decrypt.close();
    return QFile::exists(decryptName);
}

QString Utils::fileMimeType(const QString &filePath)
{
    QMimeDatabase db;
    QMimeType type = db.mimeTypeForFile(filePath);
    return type.name();
}

QByteArray Serialization::serialize(const QList<QByteArray> &list, const int &fiels_size)
{
    QByteArray serialized = "";
    for (const QByteArray &param : list)
    {
        serialized += Utils::intToByteArray(param.size(), fiels_size);
        serialized += param;
    }
    return serialized;
}

QList<QByteArray> Serialization::deserialize(const QByteArray &serialized, const int &fiels_size)
{
    if (serialized.isEmpty() || serialized.length() <= fiels_size)
    {
        return {};
    }

    QList<QByteArray> list = {};
    int pos = 0;
    while (pos < serialized.size())
    {
        bool ok = true;
        int count = serialized.mid(pos, fiels_size)
                        .toInt(&ok); // Utils::qByteArrayToInt(serialized.mid(pos, fiels_size));
        if (!ok)
            return list;
        pos += fiels_size;
        QByteArray el = serialized.mid(pos, count);
        pos += count;
        if (el.isEmpty())
            list.append(el);
        else
            list << el;
    }
    //    serialized.remove(0, pos);
    return list;
}

void Utils::wipeDataFiles()
{
    QDir("blockchain").removeRecursively();
    QDir(DfsStruct::ROOT_FOOLDER_NAME).removeRecursively();
    QDir("keystore").removeRecursively();
    QDir("tmp").removeRecursively();
    QFile("user.private").remove();
    QFile("user.private.login").remove();
    QFile(".settings").remove();
    QFile(".dsettings").remove();

    QDir dir(QDir::currentPath());
    dir.cdUp();
    QDir::setCurrent(dir.canonicalPath());
    QString dataName = Utils::dataName().replace("/", "");
    qDebug() << "wipe dataName" << dataName;
    QDir(dataName).removeRecursively();
    QDir().mkdir(dataName);

    QString shareFolder =
        QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).value(0) + "/Share";
    QDir(shareFolder).removeRecursively();

    /*
#ifdef ECONSOLE
    auto clearDir = [](const QString &dir, const QString &ignoredFile = "0") {
        QDir dirToClear(dir);
        auto filesList = dirToClear.entryInfoList(QDir::Files);

        for (auto &file : filesList)
        {
            if (file.fileName() != ignoredFile)
                QFile::remove(file.filePath());
        }
    };

    QByteArray companySection = TMP::companyActorId->right(2);
    QDir actorDir("blockchain/index/actors");
    auto dirsList = actorDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (auto &dir : dirsList)
    {
        if (dir.fileName() != companySection)
            QDir(dir.filePath()).removeRecursively();
    }

    clearDir("blockchain/index/actors/" + companySection, *TMP::companyActorId);
    clearDir("blockchain/index/blocks/0", "0");
    clearDir("keystore/personal", *TMP::companyActorId + ".key");
    clearDir("keystore/profile", *TMP::companyActorId + ".private");
    QDir("tmp").removeRecursively();
    QDir(DfsStruct::ROOT_FOOLDER_NAME).removeRecursively();
    QFile("blockchain/index/actors/.first").remove();
    QFile("blockchain/index/actors/.last").remove();
    QFile("blockchain/index/blocks/.first").remove();
    QFile("blockchain/index/blocks/.last").remove();
#else
    QDir("blockchain").removeRecursively();
    QDir(DfsStruct::ROOT_FOOLDER_NAME).removeRecursively();
    QDir("keystore").removeRecursively();
    QDir("tmp").removeRecursively();
    QFile("user.private").remove();
    QFile("user.private.login").remove();
#endif
    QFile(".extrachain.lock").remove();
    QFile(".settings").remove();
    */
}

void Utils::softWipe(const QString &currentId)
{
    auto clearDir = [](const QString &dir, const QString &ignoredFile = "0", bool isFile = true) {
        QDir dirToClear(dir);
        auto filesList = dirToClear.entryInfoList(isFile ? QDir::Files : QDir::Dirs | QDir::NoDotAndDotDot);

        for (auto &file : filesList)
        {
            qDebug() << file.fileName() << file.filePath();
            if (file.fileName() != ignoredFile)
                isFile ? QFile::remove(file.filePath()) : QDir(file.filePath()).removeRecursively();
        }
    };

    QDir("blockchain/index/blocks").removeRecursively();
    clearDir("blockchain/index/actors", currentId.right(2), false);
    clearDir("blockchain/index/actors/" + currentId.right(2), currentId, true);
    clearDir(DfsStruct::ROOT_FOOLDER_NAME, "", true);
    clearDir(DfsStruct::ROOT_FOOLDER_NAME + "/" + currentId, "profile", false);
    QFile::remove(DfsStruct::ROOT_FOOLDER_NAME + "/" + currentId + "/root");
    QFile::remove(DfsStruct::ROOT_FOOLDER_NAME + "/" + currentId + "/root.tmp");
    QDir("tmp").removeRecursively();
    QFile(".settings").remove();
}

qint64 Utils::checkMemoryFree()
{
    QStorageInfo x(qApp->applicationDirPath());
    qDebug() << "Free memory" << x.bytesFree() / 1024 / 1024 << "MB";
    return x.bytesFree();
}

qint64 Utils::checkMemoryTotal()
{
    QStorageInfo x(qApp->applicationDirPath());
    qDebug() << "Total memory" << x.bytesTotal() / 1024 / 1024 << "MB";
    return x.bytesTotal();
}

QString Utils::dataName()
{
#ifdef ECONSOLE
    return "console-data";
#endif
    QSettings settings;
    if (!settings.value("network/serverIp").isValid())
        settings.setValue("network/serverIp", Network::serverIp);
    QString serverIp = settings.value("network/serverIp").toString();

    if (serverIp == "51.68.181.53")
        return "/public-data";
    else if (serverIp == "51.68.181.52")
        return "/test-data";
    else
        return "/private-" + serverIp.replace(".", "-") + "-data";
}

QByteArray Serialization::fromMap(const QMap<QString, QByteArray> &map)
{
    QByteArray cbor;
    QCborStreamWriter writer(&cbor);

    writer.startMap(map.count());
    for (auto it = map.begin(); it != map.end(); ++it)
    {
        writer.append(it.key());
        writer.append(it.value());
    }
    writer.endMap();

    return cbor;
}

QByteArray Serialization::fromList(const QByteArrayList &list)
{
    QByteArray cbor;
    QCborStreamWriter writer(&cbor);

    writer.startArray(list.count());
    for (const QByteArray &el : list)
        writer.append(el);
    writer.endArray();

    return cbor;
}

QByteArrayList Serialization::toList(const QByteArray &data)
{
    QCborStreamReader reader(data);
    if (!reader.isArray() || !reader.isLengthKnown())
        return {};

    QByteArrayList list;
    list.reserve(reader.length());

    reader.enterContainer();
    while (reader.lastError() == QCborError::NoError && reader.hasNext())
    {
        list << reader.readByteArray().data;
        reader.next();
    }

    if (reader.lastError() != QCborError::NoError)
        return {};

    return list;
}

QMap<QString, QByteArray> Serialization::toMap(const QByteArray &data)
{
    QCborStreamReader reader(data);
    if (!reader.isMap() || !reader.isLengthKnown())
        return {};

    QMap<QString, QByteArray> map;

    reader.enterContainer();
    while (reader.lastError() == QCborError::NoError && reader.hasNext())
    {
        QString key = reader.readString().data;
        if (key.isEmpty())
            break;
        reader.next();
        QByteArray value = reader.readByteArray().data;
        map.insert(key, value);
    }

    if (reader.lastError() != QCborError::NoError)
        return {};

    return map;
}

int Serialization::length(const QByteArray &data)
{
    QByteArrayList list;

    QCborStreamReader reader(data);
    if (reader.isLengthKnown())
        return reader.length();

    return -1;
}

QByteArray Serialization::serializeMap(const QMap<QString, QByteArray> &map)
{
    auto it = map.begin();
    QByteArray res;

    while (it != map.end())
    {
        res += Serialization::serialize({ it.key().toUtf8(), it.value() });
        it++;
    }

    return res;
}

QMap<QString, QByteArray> Serialization::deserializeMap(const QByteArray &data)
{
    QMap<QString, QByteArray> map;
    QByteArrayList res = Serialization::deserialize(data);

    while (res.size() != 0)
    {
        map.insert(res.at(0), res.at(1));
        res.removeFirst();
        res.removeFirst();
    }

    return map;
}

QDebug operator<<(QDebug d, const Notification &n)
{
    d.noquote().nospace() << "Notification(time: " << QString::number(n.time)
                          << ", type: " << QString::number(n.type) << ", data: \"" << n.data << "\")";
    return d;
}
