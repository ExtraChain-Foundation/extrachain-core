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

// Deterministic synchronization tests. No live networking — each test drives the
// real sync code paths directly so it is reproducible and fast:
//   * pack-sync data path  : Registry::read_raw -> install_raw (what
//                            network_pack_request / network_pack_data_response do)
//   * wire format          : Legacy(hex) vs Canonical(decimal) round-trips, the
//                            mechanism that keeps new<->legacy peers interoperable
//   * migration            : a legacy hex section -> decimal on disk, with the
//                            transaction signature still verifying afterwards

#include <QtTest/QtTest>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "chain/actor.h"
#include "chain/dag.h" // Section
#include "chain/dag_migration.h"
#include "chain/hot_section_store.h"
#include "chain/pack.h"
#include "chain/pack_registry.h"
#include "chain/transaction.h"
#include "chat/chat.h"
#include "dfs/name_validator.h"
#include "encryption/encryption_tools.h"
#include "network/isocket_service.h"
#include "network/message_body.h"
#include "network/wire_format.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace {

    // Build a server-side registry holding `pack_count` packs of `per_pack` sections,
    // section ids contiguous from 0. Section payload is a small JSON-ish blob whose
    // content is deterministic per id.
    std::filesystem::path make_server_packs(const std::filesystem::path &dir, int pack_count, int per_pack) {
        std::filesystem::remove_all(dir);
        Pack::Registry reg(dir);
        for (int p = 0; p < pack_count; ++p) {
            std::map<SectionId, std::string> sections;
            for (int i = 0; i < per_pack; ++i) {
                int sid = p * per_pack + i;
                sections.emplace(SectionId(sid), "sec-" + std::to_string(sid));
            }
            auto w = reg.create_pack(static_cast<Pack::PackId>(p), sections);
            if (!w.has_value())
                return {};
        }
        return dir;
    }

} // namespace

class SyncTest : public QObject {
    Q_OBJECT

private slots:
    // ----- Pack-sync data path ------------------------------------------------

    // The exact transfer a syncing node performs: server serialises each pack
    // with read_raw(); the client validates+installs it with install_raw(), then
    // must be able to read every section back identically.
    void packSyncTransfersAllSections() {
        auto server_dir = std::filesystem::temp_directory_path() / "exc_sync_server";
        auto client_dir = std::filesystem::temp_directory_path() / "exc_sync_client";
        std::filesystem::remove_all(client_dir);

        const int packs = 3, per_pack = 100;
        QVERIFY(!make_server_packs(server_dir, packs, per_pack).empty());

        Pack::Registry server(server_dir);
        server.rescan();
        Pack::Registry client(client_dir, 1);

        // Client learns which packs exist (network_pack_list_response uses spans()).
        auto spans = server.spans();
        QCOMPARE(spans.size(), static_cast<std::size_t>(packs));

        // Transfer each missing pack: read_raw on server -> install_raw on client.
        for (const auto &s : spans) {
            auto raw = server.read_raw(s.id);
            QVERIFY2(raw.has_value(), "server read_raw failed");
            auto inst = client.install_raw(s.id, *raw);
            QVERIFY2(inst.has_value(), "client install_raw failed");
        }

        // Client now holds the full history and reads it identically.
        for (int sid = 0; sid < packs * per_pack; ++sid) {
            auto got = client.read_section(SectionId(sid));
            QVERIFY2(got.has_value(), qPrintable(QString("client missing section %1").arg(sid)));
            QCOMPARE(*got, std::string("sec-") + std::to_string(sid));
        }

        auto reopened = client.read_section(SectionId(0));
        QVERIFY(reopened.has_value());
        QCOMPARE(*reopened, std::string("sec-0"));

        Pack::Registry zero_sized_cache(client_dir, 0);
        zero_sized_cache.rescan();
        QVERIFY(zero_sized_cache.read_section(SectionId(0)).has_value());
        QVERIFY(zero_sized_cache.read_section(SectionId(per_pack)).has_value());
        QVERIFY(zero_sized_cache.read_section(SectionId(0)).has_value());

        // Coverage matches the server.
        auto cov = client.coverage();
        QVERIFY(cov.has_value());
        QCOMPARE(cov->first, SectionId(0));
        QCOMPARE(cov->last, SectionId(packs * per_pack - 1));

        std::filesystem::remove_all(server_dir);
        std::filesystem::remove_all(client_dir);
    }

    // install_raw must reject a corrupt payload (it validates by opening) and a
    // payload whose embedded id disagrees with the requested id — a peer must not
    // be able to poison the local store.
    void packSyncRejectsBadPayloads() {
        auto server_dir = std::filesystem::temp_directory_path() / "exc_sync_bad_server";
        auto client_dir = std::filesystem::temp_directory_path() / "exc_sync_bad_client";
        QVERIFY(!make_server_packs(server_dir, 1, 50).empty());

        Pack::Registry server(server_dir);
        server.rescan();
        Pack::Registry client(client_dir);

        auto raw = server.read_raw(0);
        QVERIFY(raw.has_value());

        // Corrupt the bytes -> checksum/format check must fail, nothing installed.
        std::string corrupt = *raw;
        corrupt[corrupt.size() / 2] ^= 0xFF;
        QVERIFY(!client.install_raw(0, corrupt).has_value());
        QVERIFY(!client.read_section(SectionId(0)).has_value());

        // Valid bytes but wrong id -> rejected (embedded id != requested id).
        QVERIFY(!client.install_raw(999, *raw).has_value());

        // A caller can apply chain-level checks before the pack becomes visible.
        QVERIFY(!client
                     .install_raw(0,
                                  *raw,
                                  [](const Pack::Reader &) {
                                      return false;
                                  })
                     .has_value());
        QVERIFY(!client.read_section(SectionId(0)).has_value());

        // The honest install still works afterwards.
        QVERIFY(client.install_raw(0, *raw).has_value());
        QVERIFY(client.read_section(SectionId(0)).has_value());

        std::filesystem::remove_all(server_dir);
        std::filesystem::remove_all(client_dir);
    }

    void packSyncInstallsOutOfOrderChunks() {
        auto server_dir = std::filesystem::temp_directory_path() / "exc_sync_window_server";
        auto client_dir = std::filesystem::temp_directory_path() / "exc_sync_window_client";
        std::filesystem::remove_all(client_dir);
        QVERIFY(!make_server_packs(server_dir, 1, 2000).empty());

        Pack::Registry server(server_dir);
        server.rescan();
        Pack::Registry client(client_dir);
        auto           raw = server.read_raw(0);
        QVERIFY(raw.has_value());

        const std::size_t chunk_size = std::max<std::size_t>(1, (raw->size() + 3) / 4);
        auto              first_size = std::min(chunk_size, raw->size());
        QVERIFY(client.install_chunk(0, 0, std::string_view(*raw).substr(0, first_size), false).has_value());

        std::vector<std::size_t> offsets;
        for (std::size_t offset = chunk_size; offset < raw->size(); offset += chunk_size) {
            offsets.push_back(offset);
        }
        std::ranges::reverse(offsets);
        for (const auto offset : offsets) {
            auto size = std::min(chunk_size, raw->size() - offset);
            QVERIFY(
                client.install_chunk(0, offset, std::string_view(*raw).substr(offset, size), false).has_value());
        }
        QVERIFY(client.install_chunk(0, raw->size(), {}, true).has_value());
        QCOMPARE(client.read_section(SectionId(0)), std::optional<std::string>("sec-0"));
        QCOMPARE(client.read_section(SectionId(1999)), std::optional<std::string>("sec-1999"));

        std::filesystem::remove_all(server_dir);
        std::filesystem::remove_all(client_dir);
    }

    void packWriterRejectsOversizedSectionRange() {
        const auto path = std::filesystem::temp_directory_path() / "exc_oversized_range.pack";
        std::filesystem::remove(path);

        std::map<SectionId, std::string> sections;
        for (std::size_t section = 0; section <= Pack::SECTIONS_PER_PACK; ++section) {
            sections.emplace(SectionId(static_cast<long long>(section)), "section");
        }

        const auto result = Pack::write(path, 0, sections);
        QVERIFY(!result.has_value());
        QCOMPARE(result.error(), Pack::Error::InvalidFormat);
        QVERIFY(!std::filesystem::exists(path));
    }

    void hotSectionStorePersistsAndPrunesRevisions() {
        const auto dir  = std::filesystem::temp_directory_path() / "exc_hot_section_store";
        const auto path = dir / "HotSections.db";
        std::filesystem::remove_all(dir);

        {
            HotSectionStore store(path);
            QVERIFY(store.is_open());
            QVERIFY(store.put(SectionId(10), "section-10-v1"));
            QVERIFY(store.put_many({ { SectionId(11), "section-11" }, { SectionId(12), "section-12" } }));
            QVERIFY(store.put(SectionId(10), "section-10-v2"));
            QVERIFY(store.commit_batch({ { SectionId(20), "section-20" }, { SectionId(21), "section-21" } },
                                       std::pair { SectionId(10), SectionId(21) }));
            const std::optional<std::pair<SectionId, SectionId>> expected_committed_range =
                std::pair { SectionId(10), SectionId(21) };
            QCOMPARE(store.committed_range(), expected_committed_range);
            QVERIFY(!store.commit_batch({ { SectionId(-1), "invalid" }, { SectionId(22), "section-22" } },
                                        std::pair { SectionId(10), SectionId(22) }));
            QVERIFY(!store.contains(SectionId(22)));
            QCOMPARE(store.committed_range(), expected_committed_range);
            QCOMPARE(store.get(SectionId(10)), std::optional<std::string>("section-10-v2"));

            const auto range = store.read_range(SectionId(10), SectionId(11));
            QCOMPARE(range.size(), static_cast<std::size_t>(2));
            QCOMPARE(range.at(SectionId(11)), std::string("section-11"));
            const std::optional<std::pair<SectionId, SectionId>> expected_bounds =
                std::pair { SectionId(10), SectionId(21) };
            QCOMPARE(store.bounds(), expected_bounds);

            QVERIFY(store.erase_range(SectionId(10), SectionId(10)));
            QVERIFY(!store.contains(SectionId(10)));
            QVERIFY(store.erase_from(SectionId(12)));
            QVERIFY(!store.contains(SectionId(12)));
            const std::optional<std::pair<SectionId, SectionId>> expected_rewound_range =
                std::pair { SectionId(10), SectionId(12) };
            QCOMPARE(store.committed_range(), expected_rewound_range);
        }

        {
            HotSectionStore reopened(path);
            QCOMPARE(reopened.get(SectionId(11)), std::optional<std::string>("section-11"));
            const std::optional<std::pair<SectionId, SectionId>> expected_committed_range =
                std::pair { SectionId(10), SectionId(12) };
            QCOMPARE(reopened.committed_range(), expected_committed_range);
            QVERIFY(reopened.clear());
            QVERIFY(!reopened.contains(SectionId(11)));
            QVERIFY(!reopened.bounds().has_value());
            QVERIFY(!reopened.committed_range().has_value());
        }

        std::filesystem::remove_all(dir);
    }

    // ----- Wire format (legacy interop) --------------------------------------

    // A number must round-trip through msgpack under each scope, and the two
    // encodings must differ for a value that is ambiguous between bases — proving
    // the format is chosen explicitly (by scope), never sniffed from content.
    void wireFormatRoundTrip() {
        BigNumber n(255); // decimal "255", hex "ff"

        std::string decimal_wire, hex_wire;
        {
            WireFormat::Scope s(WireFormat::Mode::Canonical);
            decimal_wire = MessagePack::serialize(n);
        }
        {
            WireFormat::Scope s(WireFormat::Mode::Legacy);
            hex_wire = MessagePack::serialize(n);
        }
        QVERIFY(decimal_wire != hex_wire); // "255" vs "ff"

        // Decode each under the matching scope -> original value.
        {
            WireFormat::Scope s(WireFormat::Mode::Canonical);
            auto              back = MessagePack::deserialize<BigNumber>(decimal_wire);
            QVERIFY(back.has_value());
            QCOMPARE(back->to_string(), std::string("255"));
        }
        {
            WireFormat::Scope s(WireFormat::Mode::Legacy);
            auto              back = MessagePack::deserialize<BigNumber>(hex_wire);
            QVERIFY(back.has_value());
            QCOMPARE(back->to_string(), std::string("255"));
        }
    }

    // The ambiguity the fix removes: a legacy peer's hex "100" means 256. Decoding
    // it under the Legacy scope yields 256; the strict-decimal constructor would
    // read the same characters as 100. This is exactly why receive handlers parse
    // under WireFormat::wire().
    void wireFormatLegacyHexIsUnambiguous() {
        BigNumber   v(256);
        std::string hex_wire;
        {
            WireFormat::Scope s(WireFormat::Mode::Legacy);
            hex_wire = MessagePack::serialize(v);
        }

        WireFormat::Scope s(WireFormat::Mode::Legacy);
        auto              decoded = MessagePack::deserialize<BigNumber>(hex_wire);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->to_string(), std::string("256"));

        // And the strict decimal constructor never sniffs hex.
        QCOMPARE(BigNumber("100").to_string(), std::string("100"));
        QCOMPARE(BigNumber::from_hex("100").to_string(), std::string("256"));
    }

    // Existing desktop and mobile clients select the display or input base
    // explicitly. These overloads must not change the canonical decimal default.
    void numeralBaseCompatibility() {
        const BigNumber integer("100");
        QCOMPARE(integer.to_string(), std::string("100"));
        QCOMPARE(integer.to_string(NumeralBase::Dec), std::string("100"));
        QCOMPARE(integer.to_string(NumeralBase::Hex), std::string("64"));
        QCOMPARE(BigNumber("100", NumeralBase::Hex).to_string(), std::string("256"));

        auto numeric_hex = BigNumber::create("100", NumeralBase::Hex);
        QVERIFY(numeric_hex.has_value());
        QCOMPARE(numeric_hex->to_string(), std::string("256"));
        QVERIFY(!BigNumber::create("10g", NumeralBase::Hex).has_value());

        const BigNumberFloat decimal("1.25", NumeralBase::Dec);
        QCOMPARE(decimal.to_string(NumeralBase::Dec), std::string("1.25"));

        auto float_hex = BigNumberFloat::create("a.5", NumeralBase::Hex);
        QVERIFY(float_hex.has_value());
        QCOMPARE(float_hex->to_string(), std::string("10.5"));
        QVERIFY(!BigNumberFloat::create("a.5.1", NumeralBase::Hex).has_value());

        QCOMPARE(BigNumber("12345").to_printable_string(), std::string("12345"));
        QCOMPARE(BigNumber("1234567").to_printable_string(), std::string("1 234 567"));
        QCOMPARE(BigNumber("-1234567").to_printable_string(), std::string("-1 234 567"));
    }

    void encryptionRoundTripsWithoutIntermediateFormats() {
        const Bytes data { 'E', 'x', 't', 'r', 'a', 'C', 'h', 'a', 'i', 'n' };
        const auto  key = Cryptography::keygen();

        for (const bool nonce_from_key : { false, true }) {
            auto encrypted = Cryptography::symmetric_encrypt(data, key, nonce_from_key);
            QVERIFY(encrypted.has_value());
            const auto expected_overhead =
                crypto_secretbox_MACBYTES + (nonce_from_key ? 0 : crypto_secretbox_NONCEBYTES);
            QCOMPARE(encrypted->size(), data.size() + expected_overhead);

            auto decrypted = Cryptography::symmetric_decrypt(*encrypted, key, nonce_from_key);
            QVERIFY(decrypted.has_value());
            QCOMPARE(*decrypted, data);

            encrypted->back() ^= 0x01;
            QVERIFY(!Cryptography::symmetric_decrypt(*encrypted, key, nonce_from_key).has_value());
        }

        const auto [sender_private, sender_public]     = Cryptography::asymmetric_create_pair();
        const auto [receiver_private, receiver_public] = Cryptography::asymmetric_create_pair();
        auto encrypted = Cryptography::asymmetric_encrypt(data, sender_private, receiver_public);
        QVERIFY(encrypted.has_value());
        QCOMPARE(encrypted->size(), data.size() + Cryptography::MIN_ENCRYPTED_SIZE_ASYMMETRIC);

        auto decrypted = Cryptography::asymmetric_decrypt(*encrypted, receiver_private, sender_public);
        QVERIFY(decrypted.has_value());
        QCOMPARE(*decrypted, data);

        auto self_encrypted = Cryptography::asymmetric_encrypt_self(data, sender_private, sender_public);
        QVERIFY(self_encrypted.has_value());
        auto self_decrypted =
            Cryptography::asymmetric_decrypt_self(*self_encrypted, sender_private, sender_public);
        QVERIFY(self_decrypted.has_value());
        QCOMPARE(*self_decrypted, data);
    }

    void dfsNameValidationMatchesCrossPlatformRules() {
        QVERIFY(NameValidator::validate("normal-file.txt").has_value());
        QVERIFY(NameValidator::validate("привет.txt").has_value());

        auto reserved = NameValidator::validate("cOm1.txt");
        QVERIFY(!reserved.has_value());
        QCOMPARE(reserved.error().code, NameValidator::ErrorCode::ReservedName);

        QVERIFY(NameValidator::validate(".hidden").has_value());

        auto leading_space = NameValidator::validate(" file.txt");
        QVERIFY(!leading_space.has_value());
        QCOMPARE(leading_space.error().code, NameValidator::ErrorCode::LeadingDotSpace);

        auto trailing_space = NameValidator::validate("file.txt ");
        QVERIFY(!trailing_space.has_value());
        QCOMPARE(trailing_space.error().code, NameValidator::ErrorCode::TrailingDotSpace);

        QVERIFY(PathValidator::validate("C:\\folder\\file.txt").has_value());
        QVERIFY(!PathValidator::validate("folder//file.txt").has_value());
    }

    void networkMessageEnvelopeRoundTrip() {
        const SocketService::HandshakeMessage legacy_handshake;
        QCOMPARE(legacy_handshake.socket_mode, SocketMode::Full);
        QCOMPARE(legacy_handshake.dfs_mode, DfsMode::Full);

        const ActorId sender("1");
        auto          message = make_init_message("payload",
                                         SendMode::Focused,
                                         MessageType::Custom,
                                         MessageStatus::Request,
                                         sender,
                                         "123456789012345",
                                         "node-a");
        message.nodes_identifiers_to_ignore.emplace("node-b");

        const auto serialized = message.serialize();
        auto       decoded    = MessagePack::deserialize<MessageBody>(serialized);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->send_type, message.send_type);
        QCOMPARE(decoded->message_type, message.message_type);
        QCOMPARE(decoded->status, message.status);
        QCOMPARE(decoded->message_id, message.message_id);
        QCOMPARE(decoded->sender_id, message.sender_id);
        QCOMPARE(decoded->data, message.data);
        QCOMPARE(decoded->nodes_identifiers_to_ignore, message.nodes_identifiers_to_ignore);
        QCOMPARE(decoded->calculate_hash(), message.calculate_hash());
    }

    void chatFolderSerializationRoundTrip() {
        const Chat::ChatFolder folder { .id              = "work",
                                        .name            = "Work",
                                        .emoji           = "briefcase",
                                        .chat_ids        = { "owner-a:file-a", "owner-b:file-b" },
                                        .pinned_chat_ids = { "owner-b:file-b" },
                                        .include_types =
                                            std::vector<Chat::ChatType> { Chat::Dialogue, Chat::Group },
                                        .excluded_chat_ids = { "owner-c:file-c" },
                                        .unread_only       = true,
                                        .muted             = false,
                                        .color             = "blue",
                                        .order             = 3 };

        const auto serialized = Json::serialize(folder);
        auto       decoded    = Json::deserialize<Chat::ChatFolder>(serialized);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->id, folder.id);
        QCOMPARE(decoded->name, folder.name);
        QCOMPARE(decoded->chat_ids, folder.chat_ids);
        QCOMPARE(decoded->pinned_chat_ids, folder.pinned_chat_ids);
        QCOMPARE(decoded->include_types, folder.include_types);
        QCOMPARE(decoded->excluded_chat_ids, folder.excluded_chat_ids);
        QCOMPARE(decoded->order, folder.order);
    }

    // ----- Migration round-trip ----------------------------------------------

    // A legacy (hex) section on disk, converted the way DagMigration does
    // (deserialize under Legacy -> serialize under Canonical), must end up decimal
    // AND keep its transaction's signature valid. Signatures are computed over the
    // hex hash, so a correct hex<->decimal migration preserves them.
    void migrationPreservesValuesAndSignature() {
        Actor<KeyPrivate> signer;
        signer.create(ActorType::User);
        auto signer_pub = signer.to_public();

        Transaction tx;
        tx.set_section(SectionId(256)); // all-digit in hex ("100") — the tricky case
        tx.set_type(TransactionType::Regular);
        tx.set_sender(signer.id());
        tx.set_receiver(signer.id());
        tx.set_token(ActorId());
        tx.set_amount(BigNumberFloat("100"));
        tx.set_timestamp(1774951775152ULL);
        QVERIFY2(tx.sign(signer), "sign failed");
        QVERIFY2(tx.verify(signer_pub), "fresh signature must verify");

        Section section { .id = SectionId(256), .transactions = { tx } };

        // Legacy on-disk form: hex-encoded numeric fields.
        std::string legacy_json;
        {
            WireFormat::Scope s(WireFormat::Mode::Legacy);
            legacy_json = Json::serialize(section);
        }
        // It really is hex: the amount 100 appears as "64".
        QVERIFY(legacy_json.find("\"64\"") != std::string::npos);

        // Migration step: read as legacy, write as canonical.
        std::string canonical_json;
        {
            WireFormat::Scope s(WireFormat::Mode::Legacy);
            auto              parsed = Json::deserialize<Section>(legacy_json);
            QVERIFY2(parsed.has_value(), "legacy section must parse under Legacy scope");
            WireFormat::Scope c(WireFormat::Mode::Canonical);
            canonical_json = Json::serialize(*parsed);
        }
        // Now decimal on disk: amount is "100", not "64".
        QVERIFY(canonical_json.find("\"100\"") != std::string::npos);
        QVERIFY(canonical_json.find("\"64\"") == std::string::npos);

        // Read back the migrated section the way the running node does (Canonical)
        // and confirm values + signature survived the hex->decimal conversion.
        WireFormat::Scope read_scope(WireFormat::Mode::Canonical);
        auto              migrated = Json::deserialize<Section>(canonical_json);
        QVERIFY2(migrated.has_value(), "migrated section must parse under Canonical scope");
        QCOMPARE(migrated->transactions.size(), static_cast<std::size_t>(1));

        const Transaction &mtx = *migrated->transactions.begin();
        QCOMPARE(mtx.amount().to_string(), std::string("100"));
        QVERIFY2(mtx.verify(signer_pub), "signature must still verify after migration");
    }

    void migrationKeepsRecoverableLegacyBackup() {
        namespace fs             = std::filesystem;
        const auto original_path = fs::current_path();
        const auto root          = fs::temp_directory_path() / "exc_staged_migration";
        fs::remove_all(root);
        fs::create_directories(root / "dag" / "0");
        fs::current_path(root);

        Transaction tx;
        tx.set_section(SectionId(0));
        tx.set_type(TransactionType::Genesis);
        tx.set_sender(ActorId());
        tx.set_receiver(ActorId());
        Section section { .id = SectionId(0), .transactions = { tx } };

        {
            WireFormat::Scope legacy(WireFormat::Mode::Legacy);
            std::ofstream     section_file(root / "dag" / "0" / "0", std::ios::binary);
            section_file << Json::serialize(section);
            std::ofstream range_file(root / "dag" / "range", std::ios::binary);
            range_file << Json::serialize(SectionRange { .first = "0", .last = "0", .last_cached = "0" });
        }

        const auto migrated = DagMigration::migrate();
        fs::current_path(original_path);

        QVERIFY2(migrated.has_value(), "staged migration must succeed");
        QVERIFY(fs::exists(root / "dag" / "hot" / "0"));
        QVERIFY(fs::exists(root / "dag.legacy-backup" / "0" / "0"));
        QVERIFY(!fs::exists(root / "dag.migration-staging"));

        fs::remove_all(root);
    }

    void migrationRecoversInterruptedActivation() {
        namespace fs             = std::filesystem;
        const auto original_path = fs::current_path();
        const auto root          = fs::temp_directory_path() / "exc_staged_migration_recovery";
        fs::remove_all(root);
        fs::create_directories(root / "dag" / "0");
        fs::current_path(root);

        Transaction tx;
        tx.set_section(SectionId(0));
        tx.set_type(TransactionType::Genesis);
        Section section { .id = SectionId(0), .transactions = { tx } };

        {
            WireFormat::Scope legacy(WireFormat::Mode::Legacy);
            std::ofstream     section_file(root / "dag" / "0" / "0", std::ios::binary);
            section_file << Json::serialize(section);
            std::ofstream range_file(root / "dag" / "range", std::ios::binary);
            range_file << Json::serialize(SectionRange { .first = "0", .last = "0", .last_cached = "0" });
        }

        fs::copy(root / "dag", root / "dag.migration-staging", fs::copy_options::recursive);
        std::ofstream(root / "dag.migration-staging" / "copy.complete") << "complete";
        fs::rename(root / "dag", root / "dag.legacy-backup");

        QVERIFY(DagMigration::needs_migration());
        const auto migrated = DagMigration::migrate();
        fs::current_path(original_path);

        QVERIFY2(migrated.has_value(), "migration must resume between activation renames");
        QVERIFY(fs::exists(root / "dag" / "hot" / "0"));
        QVERIFY(fs::exists(root / "dag.legacy-backup" / "0" / "0"));
        QVERIFY(!fs::exists(root / "dag.migration-staging"));

        fs::remove_all(root);
    }
};

QTEST_MAIN(SyncTest)
#include "sync_test.moc"
