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

#ifndef FILEUPDATERMANAGER_H
#define FILEUPDATERMANAGER_H

#include <QList>
#include <QByteArray>
#include <QObject>

#include "dfs/types/headers/dfstruct.h"
#include "utils/db_connector.h"

class FileUpdaterManager : public QObject
{
    Q_OBJECT

public:
    FileUpdaterManager(QObject *parent = nullptr);

    ~FileUpdaterManager();

public:
    void checkAllFiles();
    void checkUserFiles(const QByteArray &userId);
    void verifyMyFiles(const QByteArray &userId);
    void checkRoot(const QString &userId, const QString &ver);

public slots:
    //    void process();

signals:
    void editDB(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                QByteArrayList sqlChanges);
    void sendGetNewVersFile(const QByteArray &filePath);
    //    void finished();

private:
    void sendEditDB(const QByteArray &filePath, const QByteArray &nameTable, const QString &userId,
                    const QString &nameFile, const DfsStruct::Type &type, const QByteArrayList &listProve);
    void checkVersionFile(const QByteArray &filePath, const QByteArray &nameTable,
                          const QByteArrayList &listVerify);

    // --DFS--
    // Card Files
    const QByteArrayList cardFile { "key", "id", "type", "prevId", "nextId", "version", "sign" };
    const QByteArrayList userNamesCompany { "actorId", "username", "sign" };
    // Chat Files
    const QByteArrayList chatUser { "userId" };
    const QByteArrayList chatMessage { "messId", "userId", "message", "type", "session", "date" };
    // Stored Files
    const QByteArrayList stored { "hash", "data", "range", "type", "userId", "sign", "prevHash" };
    // Service Files
    const QByteArrayList subscriptions { "subscription" }; // ?
    const QByteArrayList subscriber { "subscriber", "sign" };
    const QByteArrayList chatInvite { "chatId", "message", "owner" };
    // Private Files
    const QByteArrayList savedEvents { "user", "event", "name", "start", "end" };
    const QByteArrayList chatId { "chatId", "key", "owner" };
    const QByteArrayList likedEvents { "user", "event" };
    const QByteArrayList savedPosts { "user", "post" };
    const QByteArrayList notification { "time", "type", "data" };
    // Property for post/event
    const QByteArrayList propertiesFields { "version",    "sender",   "dateCreate",
                                            "dateModify", "latitude", "longitude" };
    // Post/Event Files
    const QByteArrayList postProperties = propertiesFields + QByteArrayList { "sign" };
    const QByteArrayList eventUser { "userId", "sign", "approver", "approverSign" };
    const QByteArrayList eventProperties =
        postProperties + QByteArrayList { "eventName", "type",       "locationName", "scope", "agreement",
                                          "salary",    "startEpoch", "endEpoch",     "start", "end" };
    const QByteArrayList textPost { "locale", "text", "sign" };
    const QByteArrayList attachPost { "attachId", "type", "date", "data", "sign" };
    const QByteArrayList commentsPost { "commentId", "sender", "message", "date", "sub", "sign" };
    const QByteArrayList commentsLikesPost { "commentId", "userId", "sign" };
    const QByteArrayList likesPost { "userId", "sign" };
    const QByteArrayList usersMarked { "userId", "sign" };

    // --System--
    // Blocks
    const QByteArrayList block { "type", "id", "date", "data", "prevHash", "hash" };
    const QByteArrayList genBlock = block + QByteArrayList { "prevGenHash" };
    const QByteArrayList blockSign { "actorId", "digSig", "type" };
    // Tx/GenDataRow/Tokens
    const QByteArrayList transaction { "sender",   "receiver",  "amount",  "date", "data",
                                       "token",    "prevBlock", "gas",     "hop",  "hash",
                                       "approver", "digSig",    "producer" };
    const QByteArrayList tokens { "tokenId", "name", "color", "canStaking" };
    const QByteArrayList genDataRow { "actorId", "state", "token", "type" };
};

#endif // FILEUPDATERMANAGER_H
