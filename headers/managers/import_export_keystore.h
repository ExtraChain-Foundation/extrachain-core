/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once
#include "utils/exc_utils.h"

static const std::string encryptExt =".enc";
static const QString folderEncryptExt = ".fenc";

struct Keystore {
    std::string key = "", data = "";
    std::string autologinHash = "";

    Keystore(){}
    Keystore(std::string _key, std::string _data, std::string _autologinHash)
        : key(_key)
        , data(_data)
        , autologinHash(_autologinHash){}

    std::string serialize() const {
        return Utils::bytesEncodeStdString(MessagePack::serialize(*this));
    }

    bool deserialize(const std::string &serialized) {
        if (serialized.empty()) {
            return false;
        } else {
            *this = MessagePack::deserialize<Keystore>(Utils::bytesDecodeStdString(serialized));
            return true;
        }
    }

    MSGPACK_DEFINE(key, data, autologinHash)
};

struct EncryptData{
    std::string namefile, data;

    std::string serialize() const {
        return Utils::bytesEncodeStdString(MessagePack::serialize(*this));
    }

    bool deserialize(const std::string &serialized) {
        if (serialized.empty()) {
            return false;
        } else {
            *this = MessagePack::deserialize<EncryptData>(Utils::bytesDecodeStdString(serialized));
            return true;
        }
    }

    MSGPACK_DEFINE(namefile, data)
};

class KeystoneUtil {
public:
    // Simple XOR encryption example (replace with actual encryption method)
    QByteArray xorEncryptDecrypt(const QByteArray &data, const QByteArray &key) {
        QByteArray result = data;
        for (int i = 0; i < result.size(); ++i) {
            result[i] ^= key[i % key.size()];
        }
        return result;
    }

           // Encrypt a single file
    bool encryptFile(const QString &inputPath, const QString &outputPath, const QByteArray &key, std::string& writeData) {
        QFile inputFile(inputPath);
        if (!inputFile.open(QIODevice::ReadOnly)) {
            return false;
        }
        QByteArray data = inputFile.readAll();
        QByteArray encryptedData = xorEncryptDecrypt(data, key);
        QFile outputFile(outputPath);
        if (!outputFile.open(QIODevice::WriteOnly)) {
            return false;
        }
        outputFile.write(encryptedData);
        qDebug() << "WD before: [" << writeData.size() << "].";
        writeData = encryptedData.toStdString();
        qDebug() << "WD after: [" << writeData.size() << "].";

        return true;
    }

    bool encryptFolder(const QString &folderPath, const QString &outputFolderPath, const QString &namefileExport, const QByteArray &key, const std::string &autologinHash) {
        qDebug() << "Export folder - " << folderPath;
        qDebug() << "Output folder - " << outputFolderPath;
        qDebug() << "By key - " << key;
        qDebug() << "Hash - " << QString::fromStdString(autologinHash);

        QDir dir(folderPath);
        QDir tmpEncryptFolder(QDir(QString::fromStdString("encrypt")));
        QStringList files = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        std::vector<std::string> encryptedList;

        for (const QString &file : files) {
            QString inputFilePath = dir.filePath(file);
            EncryptData encryptData;
            encryptData.namefile = file.toStdString() + encryptExt;
            QString outputFilePath = tmpEncryptFolder.filePath(file + QString::fromStdString(encryptExt));

            if (!encryptFile(inputFilePath, outputFilePath, key, encryptData.data)) {
                return false;
            }
            encryptedList.push_back(encryptData.serialize());
            qDebug() << "Size of encryptedList" << encryptedList.size();
        }

        qDebug() << "start create one file";
        std::string data = Serialization::serialize(encryptedList);

        Keystore keystore(key.toStdString(), data, autologinHash);
        qDebug() << "write to: " << QString("%1%2").arg(namefileExport).arg(folderEncryptExt);
        QString outputFilePathForFolder = QDir(outputFolderPath).filePath(QString("%1%2").arg(namefileExport).arg(folderEncryptExt));
        qDebug() << "Export to file - " << outputFilePathForFolder;

        QFile outputFile(outputFilePathForFolder);
        if (!outputFile.open(QIODevice::WriteOnly)) {
            qDebug() << "File to export folder " << outputFilePathForFolder << " not open";
            return false;
        }
        outputFile.write(QByteArray::fromStdString(keystore.serialize()));
        outputFile.flush();
        outputFile.close();

        QFile inputFile(outputFilePathForFolder);
        if (!inputFile.open(QIODevice::ReadOnly)) {
            return false;
        }

        qDebug() << "Size of keystore:" << QByteArray::fromStdString(keystore.serialize()).size();
        QByteArray encryptedData = inputFile.readAll();
        qDebug() << "Size of ecrypted data: " << encryptedData.size();

               // Remove files
        foreach (QFileInfo item, tmpEncryptFolder.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::DirsFirst)) {
            if (!item.isDir()) {
                qDebug() << "Remove file - " << item.fileName();
                QFile::remove(item.absoluteFilePath());
            }
        }

        return true;
    }

    bool decryptFile(const QString &inputPath, const QString &outputPath, const QByteArray &key) {
        QFile inputFile(inputPath);
        if (!inputFile.open(QIODevice::ReadOnly)) {
            return false;
        }
        QByteArray encryptedData = inputFile.readAll();
        QByteArray data = xorEncryptDecrypt(encryptedData, key);
        QFile outputFile(outputPath);
        if (!outputFile.open(QIODevice::WriteOnly)) {
            return false;
        }
        outputFile.write(data);
        return true;
    }

           // Decrypt all files in a folder
    bool decryptFolder(const QString &filePath, const QString &outputFolderPath, const QByteArray &key, QString& error, std::string& hash) {
        QDir dir(filePath);
        if (!filePath.endsWith(folderEncryptExt)) {
            error = "File has not extention `.fenc`";
            return false;
        }

        clearDirectory(outputFolderPath);

        QString inputFilePath = dir.filePath(filePath);
        qDebug() << filePath << inputFilePath;

        QFile inputFile(inputFilePath);
        if (!inputFile.open(QIODevice::ReadOnly)) {
            error = "File can not open.";
            return false;
        }

        QByteArray encryptedData = inputFile.readAll();
        Keystore keystore;
        keystore.deserialize(encryptedData.toStdString());
        qDebug() << "decrypt folder hash: [" << keystore.autologinHash << "]";
        hash = keystore.autologinHash;

        if(keystore.key != key.toStdString()) {
            error = "Can not import keys. Key is not valid.";
            return false;
        }

        auto deserializedList = Serialization::deserialize(keystore.data);
        qDebug() << "count files:" << deserializedList.size();
        for(auto data : deserializedList) {
            EncryptData decryptData;
            decryptData.deserialize(data);
            auto xorEncryptDecrypted = xorEncryptDecrypt(QByteArray::fromStdString(decryptData.data), key);
            QFile fileData(QDir(outputFolderPath).filePath(QString::fromStdString(decryptData.namefile).chopped(4)));
            if(fileData.open(QIODevice::WriteOnly)) {
                qDebug() << "file " << fileData.fileName() << " opened";
                fileData.write(QByteArray(xorEncryptDecrypted));
                fileData.flush();
                fileData.close();
            }
        }
        return true;
    }

protected:
    bool clearDirectory(const QString &path) {
        QDir dir(path);

        if (!dir.exists()) {
            return false; // Directory does not exist
        }

        QFileInfoList files = dir.entryInfoList(QDir::Files);
        QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

               // Remove all files in the directory
        foreach (QFileInfo file, files) {
            QFile::remove(file.absoluteFilePath());
        }

               // Recursively remove all subdirectories
        foreach (QFileInfo subDir, dirs) {
            QDir subDirPath(subDir.absoluteFilePath());
            subDirPath.removeRecursively();
        }

        return true;
    }
};
