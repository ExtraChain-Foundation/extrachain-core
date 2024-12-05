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

#include <filesystem>
#include <string>
#include <vector>

#include "blockchain/actor.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"
#include "utils/db_connector.h"
#include <boost/algorithm/string/replace.hpp>
#include <fmt/format.h>
#include <msgpack.hpp>

#include "dfs/collection_template.h"
#include "utils/exc_logs.h"

namespace Tools {
    template <typename T>
    std::vector<unsigned char> typeToByteArray(T integerValue) {
        std::vector<unsigned char> res;
        unsigned char*             b = (unsigned char*)(&integerValue);
        unsigned char*             e = b + sizeof(T);
        std::copy(b, e, back_inserter(res));
        return res;
    }

    template <typename T>
    T byteArrayToType(std::vector<unsigned char> value) {
        T* res;
        res = reinterpret_cast<T*>(value.data());
        return *res;
    }

    template <typename T>
    std::string typeToStdStringBytes(T integerValue) {
        std::string    res;
        unsigned char* b = (unsigned char*)(&integerValue);
        unsigned char* e = b + sizeof(T);
        std::copy(b, e, back_inserter(res));
        return res;
    }

    template <typename T>
    T stdStringBytesToType(std::string value) {
        T* res;
        res = reinterpret_cast<T*>(value.data());
        return *res;
    }
} // namespace Tools

namespace Utils {
    std::string platformDelimeter();

    inline static std::uint64_t globalVariableOfDfsSize = 0;
} // namespace Utils

namespace Dfs {
    namespace Basic {
        static const std::string   fsActrRoot                 = "dfs";
        static const std::wstring  fsActrRootW                = L"dfs";
        static const std::string   fsMapName                  = ".dir";
        static const std::string   dirsPath                   = "dfs/.dirs";
        static const std::string   COLLECTION_FILE            = ".collection";
        static const std::uint64_t sectionSize                = /*2097152*/ 524228;
        static const std::uint64_t maxSectionSize             = 209715200;
        static const std::uint64_t minDfsLimit                = 2147483648;
        static const std::uint64_t historicalChainSectionSize = 209715200;

        static const std::uint64_t encSectionSize   = 256;
        static std::wstring        separator        = std::wstring(1, std::filesystem::path::preferred_separator);
        static const int           miningReward     = 1;
        static const std::string   dsStoreExtention = ".DS_Store";
    } // namespace Basic

    enum class FileIdError {
        InvalidHexString,
        EmptyString
    };

    class FileId final {
    public:
        // Prevents direct construction
        static std::expected<FileId, FileIdError> create(std::string hex_string) {
            if (hex_string.empty()) {
                eLog("Attempt to create FileId with empty string");
                return std::unexpected(FileIdError::EmptyString);
            }

            if (!Utils::is_hex_string_lower(hex_string)) {
                eLog("Invalid hex string for FileId: {}", hex_string);
                return std::unexpected(FileIdError::InvalidHexString);
            }

            return FileId(std::move(hex_string));
        }

        // Getter for the hex string
        [[nodiscard]] const std::string& value() const noexcept {
            return hex_string_;
        }

        // Equality operators
        bool operator==(const FileId& other) const noexcept = default;
        bool operator!=(const FileId& other) const noexcept = default;

    private:
        explicit FileId(std::string hex_string)
            : hex_string_(std::move(hex_string)) {
        }

        std::string hex_string_;
    };

    enum class DfsError {
        Unknown,
        NotExists,
        NotFile,
        NotReadable,
        StorageFull,
        AlreadyExists,
        DirError,
        DirValueNotExists,
        CollectionCreationError,
        InvalidName,
        InvalidTemplate,
        NotWritable
    };

    enum class FileType {
        Folder     = 0,
        File       = 10,
        Collection = 20,
        Dictionary = 30
    };

    enum class FileState {
        Removed = 0,
        Ready   = 1,
        Known   = 2,
        Partial = 3
    };

    enum class SecurityLevel {
        Public    = 0,
        Encrypted = 1
    };

    struct DirRow {
        ActorId actor_id;

        std::string                file_id;
        std::optional<std::string> prev_file_id;

        std::string hash;

        std::optional<std::string> folder;
        std::string                name;

        std::size_t   size;
        std::uint64_t created       = 0;
        std::uint64_t last_modified = 0;

        Dfs::FileType      type       = Dfs::FileType::File;
        Dfs::SecurityLevel encryption = Dfs::SecurityLevel::Public;
        Dfs::FileState     state      = Dfs::FileState::Known;

        Signature sign = Signature();

        std::string visualPath() const {
            if (folder.has_value())
                return folder.value() + "/" + name;
            else
                return name;
        }

        bool isLoaded() const {
            return state == Dfs::FileState::Ready;
        }

        bool isEncrypted() const {
            return encryption == Dfs::SecurityLevel::Encrypted;
        }
    };

    BOOST_DESCRIBE_STRUCT(DirRow,
                          (),
                          (actor_id,
                           file_id,
                           prev_file_id,
                           hash,
                           folder,
                           name,
                           size,
                           created,
                           last_modified,
                           type,
                           encryption,
                           state,
                           sign))

    namespace Packets {
        struct ResponseDfsSize {
            ActorId     actorId;
            std::size_t size;

            MSGPACK_DEFINE(actorId, size)
        };

        struct RequestDfsSize {
            ActorId actorId;

            MSGPACK_DEFINE(actorId)
        };

        struct ResponseBlockCount {
            ActorId   actorId;
            BigNumber blockCount;

            MSGPACK_DEFINE(actorId, blockCount)
        };

        struct RequestBlockCount {
            ActorId actorId;

            MSGPACK_DEFINE(actorId)
        };

        struct RequestFileSegmentMessage {
            ActorId       actorId;
            std::string   file_id;
            std::string   hash;
            std::uint64_t offset;
            MSGPACK_DEFINE(actorId, file_id, hash, offset)
        };

        struct RemoveFileMessage {
            ActorId     actorId;
            std::string file_id;
            MSGPACK_DEFINE(actorId, file_id)
        };

        struct SegmentMessage {
            ActorId       actorId;
            std::string   file_id;
            std::string   hash;
            std::string   data;
            std::uint64_t offset;
            MSGPACK_DEFINE(actorId, file_id, hash, data, offset)
        };

        enum SegmentMessageType {
            Add     = 0,
            Insert  = 1,
            Replace = 2,
            Remove  = 3
        };

        struct EditSegmentMessage {
            ActorId            actorId;
            std::string        file_id;
            std::string        hash;
            std::string        newHash;
            std::string        data;
            std::uint64_t      offset;
            SegmentMessageType actionType;
            MSGPACK_DEFINE(actorId, file_id, hash, data, offset, actionType)
        };

        struct DeleteSegmentMessage {
            ActorId       actorId;
            std::string   file_id;
            std::string   hash;
            std::uint64_t offset;
            std::uint64_t size;
            MSGPACK_DEFINE(actorId, file_id, hash, offset, size)
        };

        struct VerifyFileMessage {
            ActorId       actorId;
            std::string   hash;
            std::string   file_id;
            bool          verified = false;
            std::uint64_t size;
            MSGPACK_DEFINE(actorId, file_id, hash, verified, size)
        };

        struct Connection {
            std::string port;
            std::string address;
            bool        active;
            MSGPACK_DEFINE(port, address, active)
        };

        struct Activity {
            std::uint64_t timeactivity;
            bool          active;
            std::uint64_t score;
            MSGPACK_DEFINE(timeactivity, active, score)
        };

        struct WSConnection {
            std::string   address;
            std::uint64_t port;
            MSGPACK_DEFINE(address, port)
        };

        struct ReferenceData {
            std::string key;
            std::string access;

            ReferenceData() {
            }

            ReferenceData(std::string _key, std::string _access)
                : key(_key)
                , access(_access) { };

            std::string toString() const {
                return std::string(fmt::format("[\"key\":\"{}\",\"access\":\"{}\"]", key, access));
            }
        };
    } // namespace Packets

    namespace Fragments {
        static const std::string Extension          = ".fragments";
        static const std::string TableNameFragments = "Fragments";
        static const std::string CreateTableQueryFragments = "CREATE TABLE IF NOT EXISTS " + TableNameFragments
                                                     + "("
                                                       "pos        INTEGER PRIMARY KEY NOT NULL, "
                                                       "storedPos  INTEGER             NOT NULL, "
                                                       "size       INTEGER             NOT NULL, "
                                                       "fragHash   TEXT                NOT NULL"
                                                       ");";

        static const std::string GetCountFragmants = "SELECT COUNT(size) FROM Fragments";
        static const std::string GetSizeFragmants  = "SELECT SUM(size) FROM Fragments";

        struct FragmentsInfo {
            ActorId                        actor;
            std::string                    fileHash;
            std::string                    filePath;
            std::uint64_t                  fileSize;
            std::list<std::pair<int, int>> fragmentPositionList;

            void print() const {
                eLog("[Fragment] actor: {}, file hash: {}, path: {}", actor, fileHash, filePath);
                for (const auto& pair : fragmentPositionList) {
                    eLog("{} {}", pair.first, pair.second);
                }
            }

            MSGPACK_DEFINE(actor, fileHash, filePath, fileSize, fragmentPositionList)
        };
    } // namespace Fragments

    namespace Historical {
        struct FileChange {
            std::uint64_t pos;
            std::string   data;

            std::string toString() {
                return Tools::typeToStdStringBytes<std::uint64_t>(pos) + data;
            }

            void fromStdString(std::string string) {
                pos  = Tools::stdStringBytesToType<std::uint64_t>(string.substr(0, 8));
                data = string.substr(8);
            }
        };

        static const std::string HISTORICAL_TABLE = "historical_chain";

        static const std::string TableNameHC = "HistoricalChain";
        static const std::string CreateTableHistoricalChain = "CREATE TABLE IF NOT EXISTS " + TableNameHC
                                                      + "("
                                                        "num        INTEGER PRIMARY KEY NOT NULL,"
                                                        "prevNum    INTEGER             NOT NULL,"
                                                        "type       INTEGER             NOT NULL,"
                                                        "data       BLOB                NOT NULL,"
                                                        "hash       TEXT                NOT NULL "
                                                        ");";
    } // namespace Historical

    namespace Reward {
        static const BigNumber coinProductionAlgorithmTick = BigNumber("20", NumeralBase::Dec); // 100
        struct CoinReward {
            ActorId        Actor;
            BigNumberFloat Coin;
            MSGPACK_DEFINE(Actor, Coin)
        };

        enum TypeFunctioning {
            Base,
            Test
        };

        struct RequestReward {
            ActorId         Actor;
            std::uint64_t   DataStoredSize;
            TypeFunctioning TypeFunctioningObj;
            BigNumberFloat  RewardAmount;
            std::uint64_t   BytesSent;
            std::uint64_t   BytesReceived;
            BigNumber       BlocksStored;
            MSGPACK_DEFINE(Actor,
                           DataStoredSize,
                           TypeFunctioningObj,
                           RewardAmount,
                           BytesSent,
                           BytesReceived,
                           BlocksStored)
        };
    } // namespace Reward

    namespace Tables {
        namespace ActorDirFile {
            static const std::string TableName = "Files";
            static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
    + "("
      "file_id       TEXT PRIMARY KEY  NOT NULL,"
      "prev_file_id  TEXT              UNIQUE,"
      "actor_id      TEXT              NOT NULL,"
      "hash          TEXT              NOT NULL,"
      "folder        TEXT                     ,"
      "name          TEXT              NOT NULL,"
      "size          INTEGER           NOT NULL,"
      "created       INTEGER           NOT NULL,"
      "last_modified INTEGER           NOT NULL,"
      "type          INTEGER           NOT NULL CHECK (type BETWEEN 0 AND 39),"
      "encryption    INTEGER           NOT NULL CHECK (encryption BETWEEN 0 AND 1),"
      "state         INTEGER           NOT NULL CHECK (state BETWEEN 0 AND 3),"
      "sign          TEXT              NOT NULL"
      ");";

            std::vector<DbRow> getFileDataByName(DbConnector* db, std::string name);
            std::string        getLastFileId(DbConnector& db);
            int                totalFileSize(const ActorId& actorId);
            std::uint64_t      dataAmountStoredSize(const ActorId& actorId, const std::string& storjName);

            // TODO: expected
            DbConnector get_actor_dir_file(const ActorId& actorId);

            std::filesystem::path actorDbPath(const ActorId& actorId);
            std::filesystem::path storjDbPath(const ActorId& actorId, const std::string& storjName);

            // TODO: field: string to enum class
            std::expected<Dfs::DirRow, Dfs::DfsError>              get_dir_row(const ActorId&     actor_id,
                                                                               const std::string& search_value,
                                                                               const std::string& field = "file_id");
            std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> get_dir_rows(const ActorId& actorId,
                                                                                std::uint64_t  last_modified = 0);

            // TODO: expected
            std::optional<Dfs::CollectionTemplate> get_collection_template_file_id(const ActorId& actor_id,
                                                                                   std::string    file_id);
            std::optional<Dfs::CollectionTemplate> get_collection_template_name(const ActorId&     actor_id,
                                                                                const std::string& template_name);
            bool                                   add_dir_row(const ActorId&                            actor_id,
                                                               DirRow&                                   dir_row,
                                                               const std::shared_ptr<Actor<KeyPrivate>>& signer);
            bool add_dir_rows(const ActorId& actor_id, const std::vector<Dfs::DirRow>& dir_rows);

            std::pair<std::string, uint64_t> calculate_collection_hash_size(const ActorId&     actor_id,
                                                                            const std::string& file_id);
            bool                             update_file_metadata(const ActorId& actor_id, DirRow& dir_row);
        } // namespace ActorDirFile

        namespace DirsFile {
            static const std::string TableName = "Dirs";
            static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
                                            + "("
                                              "actorId      TEXT PRIMARY KEY NOT NULL,"
                                              "last_modified INTEGER          NOT NULL "
                                              ");";
        } // namespace DirsFile

        static const std::string permissionTable = "PermissionTable";
        static const std::string permissionTableCreate = "CREATE TABLE IF NOT EXISTS " + permissionTable
                                                 + " ("
                                                   "fileHash   TEXT NOT NULL, "
                                                   "permission TEXT NOT NULL, "
                                                   "userId     TEXT NOT NULL,"
                                                   "signature  TEXT NOT NULL"
                                                   ");";

        static const std::string filesTableLast = "WITH end_files AS ("
            "SELECT f1.file_id, f1.prev_file_id, COUNT(*) OVER() as cnt "
            "FROM " + Dfs::Tables::ActorDirFile::TableName + " f1 "
            "LEFT JOIN " + Dfs::Tables::ActorDirFile::TableName + " f2 ON f1.file_id = f2.prev_file_id "
            "WHERE f2.prev_file_id IS NULL "
            ") "
            "SELECT file_id FROM end_files WHERE cnt = 1";

        static const std::string filesTableFull = "SELECT * FROM " + Dfs::Tables::ActorDirFile::TableName;
    } // namespace Tables

    namespace Path {
        std::filesystem::path          filePath(const ActorId& actor_id, const std::string& file_id);
        std::expected<FsPath, FsError> file_path(const ActorId& actor_id, const std::string& file_id);
        std::filesystem::path          actorPath(const ActorId& actorId);
    } // namespace Path
} // namespace Dfs

MAKE_CUSTOM_MAGICAL(Dfs::FileId)

namespace DfsP    = Dfs::Packets;
namespace DfsF    = Dfs::Fragments;
namespace DfsT    = Dfs::Tables;
namespace DfsHc   = Dfs::Historical;
namespace DfsB    = Dfs::Basic;
namespace DfsPath = Dfs::Path;

// FORMAT_ENUM(Dfs::DfsError)
// FORMAT_ENUM(Dfs::FileType)
// FORMAT_ENUM(Dfs::FileState)
// FORMAT_ENUM(Dfs::Encryption)
// FORMAT_ENUM(Dfs::Packets::SegmentMessageType)
// FORMAT_ENUM(Dfs::Reward::TypeFunctioning)

MSGPACK_ADD_ENUM(Dfs::FileType)
MSGPACK_ADD_ENUM(Dfs::FileState)
MSGPACK_ADD_ENUM(Dfs::SecurityLevel)
MSGPACK_ADD_ENUM(Dfs::Packets::SegmentMessageType)
MSGPACK_ADD_ENUM(Dfs::Reward::TypeFunctioning)
