#include <filesystem>
#include <memory>

#include "chain/actor_index.h"
#include "chain/dag.h"
#include "chain/private_profile.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "managers/luminance_manager.h"
#include "network/network_service.h"
#include "test_support.h"
#include "utils/file_io.h"
#include "utils/db_iterator.h"

namespace {
    class VerificationSocket final : public SocketService {
    public:
        explicit VerificationSocket(PeerContext& context)
            : SocketService(context) {
            ip_ = "127.0.0.2";
        }
        using SocketService::check_first_message;
        using SocketService::generate_first_message;
        bool is_active() const override {
            return activated_.load();
        }
        std::string protocol_string() const override {
            return "verification-test";
        }
        Network::Protocol protocol() const override {
            return Network::Protocol::WebSocket;
        }
        std::uint16_t port() const override {
            return 0;
        }
        std::uint16_t server_port() const override {
            return 0;
        }
        void flush() override {
        }
        void send_message(std::span<const std::uint8_t>, Priority) override {
        }
    };
    class CapturingSender final : public ResponseSender {
    public:
        std::size_t responses = 0;

        std::string send_response(const std::string&,
                                  MessageType,
                                  SendMode,
                                  MessageStatus,
                                  const Responder&) override {
            ++responses;
            return { };
        }
    };
} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestSupport::Runner    tests;
    const std::string_view selected = argc > 1 ? argv[1] : "";
    std::size_t            executed = 0;
    const auto             run      = [&](std::string_view name, auto test) {
        if (selected.empty() || name.find(selected) != std::string_view::npos) {
            ++executed;
            tests.run(name, test);
        }
    };
    run("checked integer parsing rejects malformed and oversized inputs", [] {
        for (const auto& invalid : { std::string("-"),
                                     std::string("0x12"),
                                     std::string("12g"),
                                     std::string(" 12"),
                                     std::string("+12"),
                                     std::string("12\0ff", 5),
                                     std::string(4096, 'f') }) {
            TEST_REQUIRE(!BigNumber::create(invalid, NumeralBase::Hex).has_value());
        }
        TEST_REQUIRE(!BigNumber::create(std::string(4096, '9')).has_value());
        const auto positive = BigNumber::create("000aF", NumeralBase::Hex);
        const auto negative = BigNumber::create("-ff", NumeralBase::Hex);
        TEST_REQUIRE(positive.has_value() && positive.value() == 175);
        TEST_REQUIRE(negative.has_value() && negative.value() == -255);
        for (const auto mode : { WireFormat::Mode::Legacy, WireFormat::Mode::Canonical }) {
            WireFormat::Scope scope(mode);
            TEST_REQUIRE(
                !MessagePack::deserialize<BigNumber>(MessagePack::serialize(std::string(4096, '9'))).has_value());
        }
    });

    Actor<KeyPrivate> owner;
    Actor<KeyPrivate> wallet;
    owner.create(ActorType::User);
    wallet.create(ActorType::User);
    run("selected wallet changes the signing actor", [&] {
        auto profile = PrivateProfile::create(owner, wallet, "test", nullptr, false);
        TEST_REQUIRE_EQ(profile.current().id(), owner.id());
        TEST_REQUIRE(profile.change_current(wallet.id()));
        TEST_REQUIRE_EQ(profile.current().id(), wallet.id());
        TEST_REQUIRE(!profile.change_current(ActorId()));
        TEST_REQUIRE_EQ(profile.current().id(), wallet.id());
        TEST_REQUIRE_EQ(profile.system().id(), owner.id());
    });

    const auto original = std::filesystem::current_path();
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-admission-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->account_controller()->create_profile("admission-profile", ActorType::User, owner);
    TEST_REQUIRE(node->actor_index()->save_actor(wallet.to_public()).has_value());
    node->dag()->set_mode(DagMode::Full);

    run("database binding follows SQL names and preserves embedded NUL", [&] {
        DbConnector database(home / "bindings.db");
        TEST_REQUIRE(database.open());
        TEST_REQUIRE(database.query("CREATE TABLE entries (actor_id TEXT, token_id TEXT, payload TEXT)"));
        const std::string payload("plain payload");
        const DbRow       row { { "actor_id", "owner" }, { "token_id", "token" }, { "payload", payload } };
        TEST_REQUIRE(database.insert("entries", row));
        const DbRow binds { { "actor_id", "owner" }, { "token_id", "token" } };
        for (const auto query : { "SELECT * FROM entries WHERE actor_id = @actor_id AND token_id = @token_id",
                                  "SELECT * FROM entries WHERE token_id = @token_id AND actor_id = @actor_id",
                                  "SELECT * FROM entries WHERE token_id = :token_id AND actor_id = :actor_id AND "
                                  "token_id = :token_id" }) {
            const auto rows = database.select(query, "entries", binds);
            TEST_REQUIRE_EQ(rows.size(), std::size_t(1));
            TEST_REQUIRE_EQ(rows.front().at("payload"), payload);
        }
        const std::string changed("new\0payload", 11);
        TEST_REQUIRE(database.update("entries", { { "payload", changed } }, binds));
        const auto rows = database.select("SELECT * FROM entries");
        TEST_REQUIRE_EQ(rows.front().at("payload"), changed);
        auto iterator = database.select_while("SELECT payload FROM entries", "entries");
        TEST_REQUIRE(iterator != nullptr);
        TEST_REQUIRE(iterator->next());
        TEST_REQUIRE_EQ(iterator->getString(0), changed);
        TEST_REQUIRE_EQ(iterator->getValue(0), changed);
    });

    run("database numeric columns reject trailing data and nonfinite values", [&] {
        DbConnector database(home / "numeric.db");
        TEST_REQUIRE(database.open());
        TEST_REQUIRE(database.query("CREATE TABLE numbers (integer_value INTEGER, real_value REAL)"));
        for (const auto& invalid : { "12suffix", "9223372036854775808", "--1", "", "1.0" }) {
            TEST_REQUIRE(!database.insert("numbers", { { "integer_value", invalid }, { "real_value", "1" } }));
        }
        for (const auto& invalid : { "1.5suffix", "NaN", "inf", "-inf", "1e1000", "" }) {
            TEST_REQUIRE(!database.insert("numbers", { { "integer_value", "12" }, { "real_value", invalid } }));
        }
        TEST_REQUIRE(database.select("SELECT * FROM numbers").empty());
        TEST_REQUIRE(database.insert("numbers", { { "integer_value", "-12" }, { "real_value", "1.5" } }));
    });

    run("DFS queries isolate file identifiers and accept literal quotes in names", [&] {
        namespace Space = Dfs::Tables::DirsFile::ActorSpace;
        const auto  db  = node->dfs()->get_db_instance();
        Dfs::DirRow row;
        row.file_id  = std::string(64, 'a');
        row.owner_id = owner.id();
        row.actor_id = owner.id();
        row.name     = "owner's file";
        row.folder   = "owner's folder";
        row.hash     = std::string(64, 'b');
        row.state    = Dfs::FileState::Ready;
        TEST_REQUIRE(Space::add_dir_row(db, owner.id(), row, owner));
        const auto selected = Space::search_file_by_folder_and_name(db, owner.id(), row.folder.value(), row.name);
        TEST_REQUIRE(selected.has_value() && selected.value().file_id == row.file_id);
        const std::string hostile = "' OR 1=1 --";
        TEST_REQUIRE(!Space::get_dir_row(db, owner.id(), hostile).has_value());
        TEST_REQUIRE(!Space::get_dir_row(db, owner.id(), row.file_id, "file_id OR 1=1 --").has_value());
        TEST_REQUIRE(!Space::search_file_by_hash(db, owner.id(), hostile).has_value());
        Space::update_file_state(db, owner.id(), hostile, Dfs::FileState::Removed);
        Space::update_file_after_stored_remove(db, owner.id(), hostile, { }, 100);
        const auto retained = Space::get_dir_row(db, owner.id(), row.file_id);
        TEST_REQUIRE(retained.has_value());
        TEST_REQUIRE_EQ(retained.value().state, Dfs::FileState::Ready);
        TEST_REQUIRE_EQ(retained.value().name, row.name);
        TEST_REQUIRE(Dfs::Path::file_path(owner.id(), row.file_id).has_value());
        TEST_REQUIRE(!Dfs::Path::file_path(owner.id(), std::string(64, '/')).has_value());
        TEST_REQUIRE(!Dfs::Path::file_path(owner.id(), "abc").has_value());
    });

    run("peer score binds the identifier and persists the cached value", [&] {
        const NodeId peer { .actor_id = wallet.id(), .node_identifier = "x', 999, 0) --" };
        node->luminance_manager()->increment(peer);
        TEST_REQUIRE_EQ(node->luminance_manager()->read_luminance(peer), 1);
        LuminanceManager persisted(nullptr);
        TEST_REQUIRE_EQ(persisted.read_luminance(peer), 1);
    });

    run("handshake preserves the DFS mode and advertised constant flag", [] {
        SocketService::HandshakeMessage handshake;
        handshake.dfs_mode    = DfsMode::Light;
        handshake.is_constant = true;
        const auto decoded    = Json::deserialize<SocketService::HandshakeMessage>(Json::serialize(handshake));
        TEST_REQUIRE(decoded.has_value());
        TEST_REQUIRE_EQ(decoded.value().dfs_mode, DfsMode::Light);
        TEST_REQUIRE(decoded.value().is_constant);
    });

    run("unsigned peer identity cannot reserve an identifier", [&] {
        auto                            socket = std::make_shared<VerificationSocket>(*node->network());
        const auto                      local  = Json::deserialize<SocketService::HandshakeMessage>(
                                                     ByteArray(socket->generate_first_message()).toString())
                                                     .value();
        SocketService::HandshakeMessage claim;
        claim.version      = local.version;
        claim.network_id   = local.network_id;
        claim.identifier   = Utils::generate_random_hex(64);
        claim.is_available = true;
        TEST_REQUIRE(!socket->check_first_message(claim));
        TEST_REQUIRE(socket->identifier().empty());
        TEST_REQUIRE(!socket->is_active());
    });

    run("conversion checks source balance including pending debits", [&] {
        const auto  source_key = std::pair { owner.id(), owner.id() };
        Transaction initial_balance;
        initial_balance.set_type(TransactionType::Balance);
        initial_balance.set_sender(owner.id());
        initial_balance.set_receiver(owner.id());
        initial_balance.set_token(owner.id());
        initial_balance.set_section(SectionId(1));
        initial_balance.set_amount(BigNumberFloat(5));
        TEST_REQUIRE(initial_balance.sign(owner));
        TEST_REQUIRE(node->dag()->save_transaction(initial_balance));
        TEST_REQUIRE(
            node->dag()->cache().write_cached_balances({ { source_key, BigNumberFloat(5) } }, SectionId(9)));
        const auto conversion = [&](int amount, std::uint64_t timestamp) {
            Transaction tx;
            tx.set_type(TransactionType::Conversion);
            tx.set_sender(owner.id());
            tx.set_receiver(owner.id());
            tx.set_token(wallet.id());
            tx.set_meta(owner.id().to_string());
            tx.set_section(SectionId(10));
            tx.set_amount(BigNumberFloat(amount));
            tx.set_timestamp(timestamp);
            TEST_REQUIRE(tx.sign(owner));
            return tx;
        };
        const SectionId frontier(10);
        TEST_REQUIRE_EQ(node->dag()->prove_transaction(conversion(6, 1), { }, nullptr, &frontier),
                        TransactionProveError::ConversionIncorrectBalance);
        const auto funded_result = node->dag()->prove_transaction(conversion(5, 2), { }, nullptr, &frontier);
        TEST_REQUIRE_MESSAGE(funded_result == TransactionProveError::NoError,
                             std::to_string(std::to_underlying(funded_result)));
        const std::set<Transaction> pending { conversion(3, 3) };
        TEST_REQUIRE_EQ(node->dag()->prove_transaction(conversion(3, 4), { }, &pending, &frontier),
                        TransactionProveError::ConversionIncorrectBalance);
        TEST_REQUIRE_EQ(node->dag()->prove_transaction(conversion(2, 5), { }, &pending, &frontier),
                        TransactionProveError::NoError);
        const auto invalid = conversion(6, 6);
        TEST_REQUIRE(
            FileIo::write_atomic(std::filesystem::path(ChainConst::DAG_HOT_FOLDER) / "10",
                                 Json::serialize(Section { .id = SectionId(10), .transactions = { invalid } }))
                .has_value());
        const auto violation = node->dag()->cache().validate_state_to(SectionId(10));
        TEST_REQUIRE(violation.has_value());
        TEST_REQUIRE_EQ(violation.value().transaction_hash, invalid.hash());
        const auto replay =
            node->dag()->cache().update_to_genesis_section(SectionId(10),
                                                           SectionId(10),
                                                           SectionId(0),
                                                           [&](const SectionId& id) {
                                                               return node->dag()->read_section(id);
                                                           });
        TEST_REQUIRE(!replay.first);
        TEST_REQUIRE_EQ(node->dag()->cache().section(), SectionId(9));
        TEST_REQUIRE_EQ(node->dag()->cache().read_cached_balance(owner.id(), owner.id()), BigNumberFloat(5));
    });

    run("network tracing rejects malformed payloads without throwing", [&] {
        const auto previous_debug = Network::networkDebug;
        Network::networkDebug     = true;
        for (const auto& payload :
             { std::string(), std::string("\xdd\xff\xff\xff\xff", 5), std::string("\xc1", 1) }) {
            const auto body      = make_init_message(payload,
                                                     SendMode::Focused,
                                                     MessageType::Custom,
                                                     MessageStatus::NoStatus,
                                                     wallet.id(),
                                                     { },
                                                     "admission-peer");
            const auto signature = wallet.key().sign(ByteArray(body.calculate_hash()).toBytes());
            TEST_REQUIRE(signature.has_value());
            node->network()->message_received(body.serialize() + ByteArray(signature.value()).toString(),
                                              "127.0.0.1",
                                              "admission-peer");
        }
        Network::networkDebug = previous_debug;
    });

    run("legacy section requests reject invalid hex without throwing", [&] {
        node->dag()->start();
        for (const auto& invalid : { std::string("not-hex"), std::string("-"), std::string(4096, 'f') }) {
            const SectionRange range { .first = invalid, .last = "20" };
            const auto         body      = make_init_message(MessagePack::serialize(range),
                                                             SendMode::Focused,
                                                             MessageType::DagFileSections,
                                                             MessageStatus::Request,
                                                             wallet.id(),
                                                             { },
                                                             "admission-peer");
            const auto         signature = wallet.key().sign(ByteArray(body.calculate_hash()).toBytes());
            TEST_REQUIRE(signature.has_value());
            node->network()->message_received(body.serialize() + ByteArray(signature.value()).toString(),
                                              "127.0.0.1",
                                              "admission-peer");
        }
        node->dag()->stop();
    });

    run("section requests reject negative and inverted ranges", [&] {
        CapturingSender sender;
        Responder       responder(&sender);
        responder.add_identifier("admission-peer");
        for (const auto& range : { std::pair { SectionId(-20), SectionId(0) },
                                   std::pair { SectionId(0), SectionId(-1) },
                                   std::pair { SectionId(0), SectionId(1000000) } }) {
            node->dag()->network_request_file_sections(range.first, range.second, responder);
            node->dag()->network_request_control_section({ .from = range.first, .to = range.second }, responder);
            TEST_REQUIRE(!node->dag()->hash_interval(range.first, range.second).has_value());
        }
        TEST_REQUIRE_EQ(sender.responses, std::size_t(0));
    });

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    const auto result = executed == 0 ? 2 : tests.result();
    if (result == 0) {
        std::filesystem::remove_all(home);
    } else {
        std::fprintf(stderr, "Artifacts: %s\n", home.string().c_str());
    }
    return result;
}
