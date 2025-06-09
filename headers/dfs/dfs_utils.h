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

#include "chain/actor.h"
#include "chain/transaction.h"
#include "utils/bignumber.h"
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
        static const std::string   DFS_FOLDER                 = "dfs";
        static const std::wstring  fsActrRootW                = L"dfs";
        static const std::string   fsMapName                  = ".dir";
        static const std::string   dirsPath                   = "dfs/.dirs";
        static const std::string   COLLECTION_FILE            = ".collection";
        static const std::string   VECTOR_FILE                = ".vector";
        static const std::string   DICTIONARY_FILE            = ".dictionary";
        static const std::uint64_t FRAGMENT_SIZE              = 256000;
        static const std::uint64_t maxSectionSize             = 209715200;
        static const std::uint64_t minDfsLimit                = 2147483648;
        static const std::uint64_t historicalChainSectionSize = 209715200;

        static const std::uint64_t encSectionSize = 256;
        static const std::wstring  separator      = std::wstring(1, std::filesystem::path::preferred_separator);
        static const int           miningReward   = 1;

        static const std::string TEMPLATE_COLLECTION          = ":Collection";
        static const std::string TEMPLATE_COLLECTION_TEMPLATE = ":CollectionTemplate";
        static const std::string TEMPLATE_DICTIONARY          = ":Dictionary";
        static const std::string TEMPLATE_VECTOR              = ":Vector";
        static const std::string TEMPLATE_CONTRACTS           = ":Contracts";
        static const std::string TEMPLATE_CHAT                = ":Chat";
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

    struct FileLink {
        ActorId     owner_id;
        std::string file_id;

        bool operator==(const FileLink&) const = default;

        bool operator<(const FileLink& other) const {
            return std::tie(owner_id, file_id) < std::tie(other.owner_id, other.file_id);
        }

        size_t hash() const {
            return std::hash<std::string>()(owner_id.to_string() + file_id);
        }
    };
    BOOST_DESCRIBE_STRUCT(FileLink, (), (owner_id, file_id))

    struct FileLinkFragment {
        FileLink file_link;
        std::set<std::size_t> fragment_numbers;

        bool operator<(const FileLinkFragment& other) const {
            return std::tie(file_link, fragment_numbers) < std::tie(other.file_link, other.fragment_numbers);
        }

        bool operator==(const FileLinkFragment&) const = default;
    };
    BOOST_DESCRIBE_STRUCT(FileLinkFragment, (), (file_link, fragment_numbers))

    enum class DfsError {
        Unknown,
        NotExists,
        NotFile,
        NotReadable,
        StorageFull,
        AlreadyExists,
        DirError,
        DirValueNotExists,
        DirDuplicate,
        CollectionCreationError,
        InvalidName,
        InvalidTemplate,
        NotWritable,
        WrongTemplate,
        IncorrectSecurityData,
        IncorrectEncryption,
        NoOwnerActor,
        NoAuthorActor,
        MaxFileSize
    };

    enum class FileType {
        Folder     = 0,
        File       = 10,
        Collection = 20,
        Vector     = 30,
        Dictionary = 40
    };

    enum class FileState {
        Removed    = 0,
        Known      = 1,
        Ready      = 2,
        Partial    = 3,
        Processing = 4,
        Unknown    = 100
    };

    enum class DataSecurity {
        Public    = 0,
        Encrypted = 1,
        Self      = 111,
        Actor     = 222,
        Key       = 333
    };

    struct DataSecuritySelf {
        ActorId my_actor;
    };
    BOOST_DESCRIBE_STRUCT(DataSecuritySelf, (), (my_actor))

    struct DataSecurityActor {
        ActorId sender_id;
        ActorId receiver_id;
    };
    BOOST_DESCRIBE_STRUCT(DataSecurityActor, (), (sender_id, receiver_id))

    struct DataSecurityKey {
        KeyBytes key;
    };
    BOOST_DESCRIBE_STRUCT(DataSecurityKey, (), (key))

    using DataSecurityData = std::variant<std::monostate, DataSecuritySelf, DataSecurityActor, DataSecurityKey>;

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

        Dfs::FileType  type       = Dfs::FileType::File;
        bool           encryption = false;
        Dfs::FileState state      = Dfs::FileState::Known;

        Signature sign = Signature();

        std::string visual_path() const {
            if (folder.has_value()) {
                bool        is_folder     = !folder.value().empty();
                std::string visual_folder = is_folder ? folder.value() + "/" : "";
                return visual_folder + name;
            } else {
                return name;
            }
        }

        bool loaded() const {
            return state == Dfs::FileState::Ready;
        }

        bool empty() const {
            return file_id.empty() || name.empty();
        }

        std::string calculate_hash(const ActorId& owner_id);
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

    struct FileData {
        ActorId owner_id;
        DirRow  dir_row;
    };
    BOOST_DESCRIBE_STRUCT(FileData, (), (owner_id, dir_row))

    namespace Packets {
        struct FragmentData {
            ActorId     owner_id;
            std::string file_id;
            std::string   data;
            std::uint64_t offset;
            std::uint64_t current_size;
            std::size_t fragment_number;
            std::size_t full_amount_fragments;
        };
        BOOST_DESCRIBE_STRUCT(FragmentData, (), (owner_id, file_id, data, offset, current_size, fragment_number, full_amount_fragments))

        struct FileState {
            ActorId        owner_id;
            std::string    file_id;
            Dfs::FileState state = Dfs::FileState::Known;
            std::string    hash;
        };
        BOOST_DESCRIBE_STRUCT(FileState, (), (owner_id, file_id, state, hash))

        struct RemoveFile {
            ActorId       owner_id;
            std::string   file_id;
            Signature     sign;
            std::uint64_t last_modified;
        };
        BOOST_DESCRIBE_STRUCT(RemoveFile, (), (owner_id, file_id, sign, last_modified))

        struct DfsVectorContentPackage {
            ActorId            owner_id;
            std::string        file_id;
            CollectionTemplate vector_template;
            std::string        vector_file;
            std::vector<DbRow> content;
            // int offset = 0;
        };
        BOOST_DESCRIBE_STRUCT(DfsVectorContentPackage,
                              (),
                              (owner_id, file_id, vector_template, vector_file, content))

        struct VectorRowAdd {
            ActorId     owner_id;
            std::string file_id;
            DbRow       row;
        };
        BOOST_DESCRIBE_STRUCT(VectorRowAdd, (), (owner_id, file_id, row))

        struct VectorRowRemove {
            ActorId     owner_id;
            std::string file_id;
            DbRow       row;
        };
        BOOST_DESCRIBE_STRUCT(VectorRowRemove, (), (owner_id, file_id, row))

        struct ResponseDfsSize {
            ActorId     actorId;
            std::size_t size;

            MSGPACK_DEFINE(actorId, size)
        };

        struct RequestDfsSize {
            ActorId actorId;

            MSGPACK_DEFINE(actorId)
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

    namespace FragmentsOld {
        static const std::string Extension          = ".fragments";
        static const std::string TableNameFragments = "Fragments";
        static const std::string CreateTableQueryFragments = "CREATE TABLE IF NOT EXISTS " + TableNameFragments
                                                     + "("
                                                       "pos        INTEGER PRIMARY KEY NOT NULL, "
                                                       "storedPos  INTEGER             NOT NULL, "
                                                       "size       INTEGER             NOT NULL, "
                                                       "fragHash   TEXT                NOT NULL"
                                                       ");";
    } // namespace FragmentsOld

    namespace Historical {
        static const std::string HISTORICAL_TABLE = "historical_chain";
    }

    namespace HistoricalOld {
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

        static const std::string TableNameHC = "HistoricalChain";
        static const std::string CreateTableHistoricalChain = "CREATE TABLE IF NOT EXISTS " + TableNameHC
                                                      + "("
                                                        "num        INTEGER PRIMARY KEY NOT NULL,"
                                                        "prevNum    INTEGER             NOT NULL,"
                                                        "type       INTEGER             NOT NULL,"
                                                        "data       BLOB                NOT NULL,"
                                                        "hash       TEXT                NOT NULL "
                                                        ");";
    } // namespace HistoricalOld

    namespace Reward {
        static const int TOLERANCE = 100;

        enum TypeFunctioning {
            Base,
            Test
        };

        struct RequestReward {
            std::uint64_t   DataStoredSize;
            TypeFunctioning TypeFunctioningObj;
            std::uint64_t   BytesSent;
            std::uint64_t   BytesReceived;
            BigNumber       BlocksStored;
            Transaction     transaction;
            Transaction     convert;
        };

        BOOST_DESCRIBE_STRUCT(
            RequestReward,
            (),
            (DataStoredSize, TypeFunctioningObj, BytesSent, BytesReceived, BlocksStored, transaction, convert))
    } // namespace Reward

    namespace Tables {
        namespace ActorDirFile {
            static const std::string TableName = "Files";
            static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
    + "("
      "file_id       TEXT PRIMARY KEY  NOT NULL,"
      "prev_file_id  TEXT                UNIQUE,"
      "actor_id      TEXT              NOT NULL,"
      "hash          TEXT              NOT NULL,"
      "folder        TEXT                     ,"
      "name          TEXT              NOT NULL,"
      "size          INTEGER           NOT NULL,"
      "created       INTEGER           NOT NULL,"
      "last_modified INTEGER           NOT NULL,"
      "type          INTEGER           NOT NULL CHECK (type BETWEEN 0 AND 39),"
      "encryption    INTEGER           NOT NULL CHECK (encryption BETWEEN 0 AND 1),"
      "state         INTEGER           NOT NULL CHECK (state BETWEEN 0 AND 4),"
      "sign          TEXT              NOT NULL,"
      "UNIQUE(folder, name)"
      ");";

            std::vector<DbRow> getFileDataByName(DbConnector* db, std::string name);
            std::string        getLastFileId(DbConnector& db);
            std::size_t        totalFileSize(const ActorId& actorId);
            std::uint64_t      dataAmountStoredSize(const ActorId& actorId, const std::string& storjName);

            // TODO: expected
            DbConnector get_actor_dir_file(const ActorId& actorId);

            std::filesystem::path actorDbPath(const ActorId& actorId);
            std::filesystem::path storjDbPath(const ActorId& actorId, const std::string& storjName);

            // TODO: field: string to enum class
            std::expected<Dfs::DirRow, Dfs::DfsError>              get_dir_row(const ActorId&     owner_id,
                                                                               const std::string& search_value,
                                                                               const std::string& field = "file_id");
            std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> get_dir_rows(const ActorId& owner_id,
                                                                                std::uint64_t  last_modified = 0);

            std::expected<std::unordered_map<std::string, Dfs::DirRow>, Dfs::DfsError> get_dir_rows_map(
                const ActorId& owner_id,
                std::uint64_t  last_modified = 0);

            std::expected<Dfs::DirRow, Dfs::DfsError> search_file_by_folder_and_name(const ActorId&     owner_id,
                                                                                     const std::string& folder,
                                                                                     const std::string& name);

            std::expected<Dfs::DirRow, Dfs::DfsError> search_file_by_hash(const ActorId&     owner_id,
                                                                          const std::string& hash);

            std::expected<std::string, Dfs::DfsError> last_file_id(const ActorId&     owner_id,
                                                                   const std::string& file_id);

            // TODO: search in dir row: by file type, by name, get folder, ...

            std::expected<std::vector<std::uint8_t>, Utils::ContentError> get_file_content(
                const ActorId&     actor_id,
                const std::string& file_id);

            // TODO: add expected
            void update_file_state(const ActorId& actor_id, const std::string file_id, Dfs::FileState state);

            void update_file_after_stored_remove(const ActorId&     actor_id,
                                                 const std::string& file_id,
                                                 const Signature&   sign,
                                                 std::uint64_t      last_modified);

            // TODO: expected
            std::optional<Dfs::CollectionTemplate> get_collection_template_file_id(const ActorId&     actor_id,
                                                                                   const std::string& file_id);
            std::optional<Dfs::CollectionTemplate> get_collection_template_name(const ActorId&     actor_id,
                                                                                const std::string& template_name);
            bool add_dir_row(const ActorId& owner_id, DirRow& dir_row, const Actor<KeyPrivate>& signer);
            bool add_dir_rows(const ActorId& actor_id, const std::vector<Dfs::DirRow>& dir_rows);

            std::pair<std::string, uint64_t> calculate_collection_hash_size(
                const ActorId&     owner_id,
                const std::string& file_id,
                const std::string& sort_field = "actor");
            bool update_file_metadata(const ActorId& owner_id, DirRow& dir_row, bool with_sign = true);
        } // namespace ActorDirFile

        namespace DirsFile {
            static const std::string TableName = "Dirs";
            static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
                                            + "("
                                              "actor_id      TEXT PRIMARY KEY NOT NULL,"
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

        static const std::string last_file_id_query = "WITH end_files AS ("
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
        std::expected<FsPath, FsError> file_path(const ActorId& owner_id, const std::string& file_id);
        std::filesystem::path          actorPath(const ActorId& actorId);
    } // namespace Path

    namespace DirsFile {
        struct DirsRow {
            ActorId       actor_id;
            std::uint64_t last_modified;
        };
        BOOST_DESCRIBE_STRUCT(DirsRow, (), (actor_id, last_modified))

        enum class DirsError {
            Unknown,
            DirsNotOpen,
            NoRows
        };

        std::expected<DbConnector, DirsError> database();
        bool                                  create_file();

        std::expected<std::uint64_t, Dfs::DirsFile::DirsError> max_last_modified();
        std::expected<std::uint64_t, Dfs::DirsFile::DirsError> last_modified(const ActorId& actor_id);
        void update_row(const ActorId& actor_id, std::uint64_t last_modified);
        std::expected<std::vector<DirsRow>, DirsError> load_all();
        std::expected<std::vector<DirsRow>, DirsError> load_from_modified(std::uint64_t last_modified);

        bool insert(const DirsRow& dirs_row);
        void insert_vector(const std::vector<DirsRow>& dirs_rows);
    } // namespace DirsFile

    void initialize_actor_folder(const ActorId& actor_id);
} // namespace Dfs

MAKE_CUSTOM_MAGICAL(Dfs::FileId)

namespace DfsP = Dfs::Packets;
// namespace DfsF    = Dfs::FragmentsOld;
namespace DfsT = Dfs::Tables;
// namespace DfsHc   = Dfs::HistoricalOld;
namespace DfsB = Dfs::Basic;

// MSGPACK_ADD_ENUM(Dfs::FileType)
// MSGPACK_ADD_ENUM(Dfs::FileState)
// MSGPACK_ADD_ENUM(Dfs::DataSecurity)
MSGPACK_ADD_ENUM(Dfs::Reward::TypeFunctioning)

namespace std {
    template <>
    struct hash<Dfs::FileLink> {
        std::size_t operator()(const Dfs::FileLink& c) const {
            return c.hash();
        }
    };
} // namespace std
