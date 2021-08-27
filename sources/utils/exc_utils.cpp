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

#include "utils/exc_utils.h"

#include <QHostAddress>
#include <QMimeDatabase>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QTcpSocket>

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
    std::string rkey = SecretKey::getKeyFromPass(key.toStdString());
    while (!orig.atEnd())
    {
        QByteArray part = orig.read(blockSize);
        QByteArray encrypted = QByteArray::fromStdString(SecretKey::encrypt(part.toStdString(), rkey));
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
    std::string rkey = SecretKey::getKeyFromPass(key.toStdString());
    while (!encrypt.atEnd())
    {
        QByteArray part = encrypt.read(blockSize);
        QByteArray decrypted = QByteArray::fromStdString(SecretKey::decrypt(part.toStdString(), rkey));
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
    QString current = QDir::currentPath();

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
    QString dataName = Utils::dataDir();
    // qDebug() << "[Wipe] Remove path:" << dataName;
    QDir(dataName).removeRecursively();
    QDir().mkpath(dataName);

    QString shareFolder =
        QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).value(0) + "/Share";
    QDir(shareFolder).removeRecursively();

    QDir::setCurrent(current);

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

QString Utils::dataDir(const QString &newDir)
{
    static QString current = "extrachain-data";

    if (!newDir.isEmpty())
        current = Utils::fixFileName(newDir);

    return current;
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

std::string Utils::byteToHexString(std::vector<unsigned char> &data)
{
    size_t psize = data.size() * 2 + 1;
    std::vector<char> p(psize);
    sodium_bin2hex(p.data(), psize, data.data(), data.size());
    std::string s(p.begin(), p.end());
    // s.erase(--s.end());
    return s;
}

std::string Utils::byteToHexString(const std::string &data)
{
    std::vector<unsigned char> v(data.begin(), data.end());
    return byteToHexString(v);
}

std::string Utils::hexStringToByte(const std::string &data)
{
    std::vector<unsigned char> p;
    p.resize(data.length() / 2 + 1);
    const char *end;
    size_t size;
    int r = sodium_hex2bin(p.data(), p.size(), data.c_str(), data.length(), NULL, &size, &end);
    std::string res;
    if (r == 0)
    {
        res = std::string(p.begin(), p.end());
        res.resize(res.size() - 1);
    }
    return res;
}

QString Utils::detectCompiler()
{
#ifdef __clang__
#if __clang_major__ < 9
#error "Clang must be version 9 or higher"
#endif
#elif __GNUC__
#if __GNUC__ < 8
#error "GCC must be version 8 or higher"
#endif
#elif _MSC_VER && !__INTEL_COMPILER
#else
#error "Compiler not supported"
#endif

#if __GNUC__ > 4
    QString gcc = "GCC";
    return QString("%4 %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__).arg(gcc);
#endif

#if _MSC_VER && !__INTEL_COMPILER
    QString msvcVersion;
    msvcVersion = "MSVC " + QString::number(_MSC_FULL_VER);
    msvcVersion.insert(7, ".");
    msvcVersion.insert(10, ".");
#endif

#ifdef __clang__
    QString compiler =
        QString("Clang %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#if _MSC_VER && !__INTEL_COMPILER
    compiler += " (" + msvcVersion + ")";
#endif
    return compiler;
#endif

#if _MSC_VER && !__INTEL_COMPILER
    return msvcVersion;
#else
    return "unknown";
#endif
}

QNetworkAddressEntry Utils::findLocalIp(PrintDebug debug)
{
    const auto allInterfaces = QNetworkInterface::allInterfaces();
    const QHostAddress &localhost = QHostAddress(QHostAddress::LocalHost);
    QList<QHostAddress> localIpNotConnect;

    for (const QNetworkInterface &networkInterface : allInterfaces)
    {
        const auto entries = networkInterface.addressEntries();

        for (const QNetworkAddressEntry &address : entries)
        {
            if (address.ip().protocol() == QAbstractSocket::IPv4Protocol && address.ip() != localhost)
            {
                if (debug == PrintDebug::On)
                {
                    qDebug() << "[FindLocalIp] Find local ip candidate:" << networkInterface;
                }

                localIpNotConnect.append(address.ip());
            }
        }
    }

    for (const QNetworkInterface &networkInterface : allInterfaces)
    {
        const auto entries = networkInterface.addressEntries();

        for (const QNetworkAddressEntry &entry : entries)
        {
            const auto flags = networkInterface.flags();

            bool isLoopBack = flags.testFlag(QNetworkInterface::IsLoopBack);
            bool isPointToPoint = flags.testFlag(QNetworkInterface::IsPointToPoint);
            bool isRunning = flags.testFlag(QNetworkInterface::IsRunning);
            if (!isRunning || !networkInterface.isValid() || isLoopBack || isPointToPoint)
                continue;

            auto socket = std::make_unique<QTcpSocket>();
            socket->bind(entry.ip());
            socket->connectToHost("8.8.8.8", 53);
            if (!socket->waitForConnected(1000))
                continue;

            if (localIpNotConnect.contains(entry.ip()))
            {
                QString name = networkInterface.name();

                if (name.left(2) == "vm")
                    continue;
                if (name.left(2) == "wl" || name.left(3) == "eth" || name.left(2) == "en"
                    || name.left(8) == "wireless")
                {
                    return entry;
                }
            }
        }
    }

#ifdef QT_DEBUG
    qFatal("Can't find local ip");
    return QNetworkAddressEntry();
#else
    QNetworkAddressEntry entry;
    entry.setIp(QHostAddress::AnyIPv4);
    return entry;
#endif
}

QString Utils::fixFileName(const QString &fileName, const QString &replaceSymbol)
{
    QString fixedName = fileName.simplified();
    fixedName = fixedName.replace(QRegularExpression("[+%@!:*?/\"<>|«»]+"), replaceSymbol);
    fixedName = fixedName.replace("\\", replaceSymbol);
    return fixedName;
}
