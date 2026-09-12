#include "network/websocket_service.h"
#include "network/network_runtime.h"
#include "network/peer_identity.h"
#include "utils/exc_utils.h"
#include "test_support.h"

#include <future>
#include <thread>

using namespace std::chrono_literals;
namespace asio = boost::asio;
using ExtraChain::Core::NetworkRuntime;

namespace {
    class Context final : public PeerContext {
    public:
        Context() {
            actor.create(ActorType::User);
            nonce = Utils::generate_random_hex(64);
        }
        Actor<KeyPrivate> actor;
        std::string       nonce;
        ActorId           network;
        unsigned          activations      = 0;
        unsigned          duplicate_checks = 0;
        ActorId           local_network_id() const override {
            return network;
        }
        void adopt_network_id(const ActorId& value) override {
            network = value;
        }
        std::string local_node_identifier() const override {
            return Network::peer_identifier(actor.key().public_key(), nonce).value();
        }
        std::string local_node_nonce() const override {
            return nonce;
        }
        std::optional<Actor<KeyPublic>> local_system_actor() const override {
            return actor.to_public();
        }
        std::expected<Signature, Cryptography::CryptoError> sign_handshake(const Bytes& bytes) const override {
            return actor.key().sign(bytes);
        }
        DfsMode local_dfs_mode() const override {
            return DfsMode::Full;
        }
        bool has_active_duplicate(std::string_view, const SocketService*) override {
            ++duplicate_checks;
            return false;
        }
        int active_peer_count() const override {
            return 0;
        }
        int peer_limit() const override {
            return 10;
        }
        std::set<PeerConnection> shareable_peers(std::string_view) const override {
            return { };
        }
        void peer_authenticated(std::string_view, std::string_view) override {
            ++activations;
        }
        std::uint16_t local_server_port() const override {
            return 0;
        }
        bool peer_processing_enabled() const override {
            return true;
        }
    };

    void check_queue(bool byte_limit, bool drain = false) {
        Context        alice, bob;
        NetworkRuntime runtime({ .io_threads = 2, .storage_threads = 1, .compute_threads = 1 });
        std::promise<WebSocketService::Service> accepted_promise, connected_promise;
        std::promise<void>                      checked;
        auto                                    accepted_future  = accepted_promise.get_future();
        auto                                    connected_future = connected_promise.get_future();
        auto                                    checked_future   = checked.get_future();
        std::size_t                             received         = 0;
        auto listen = runtime.listen({ .bind_address = "127.0.0.1", .port = 0 }, [&](auto socket) {
            auto service = WebSocketService::from_accepted(runtime, std::move(socket), alice);
            if (drain) {
                service->on_message = [&](SocketService::Ptr, std::string message, std::string, std::string) {
                    if (message != std::string(65536, 'x')) {
                        if (received < 32)
                            checked.set_exception(std::make_exception_ptr(std::runtime_error("Changed payload")));
                        received = 33;
                    } else if (++received == 32) {
                        checked.set_value();
                    }
                };
            }
            accepted_promise.set_value(service);
            runtime.spawn(service->run(true));
        });
        TEST_REQUIRE(listen.has_value());
        auto connect = [&]() -> asio::awaitable<void> {
            auto result = co_await WebSocketService::connect(runtime, "127.0.0.1", listen.value(), bob);
            TEST_REQUIRE(result.has_value());
            auto service          = result.value();
            service->on_activated = [&](SocketService::Ptr socket) {
                try {
                    if (drain) {
                        const SocketService::Data data(65536, 'x');
                        for (unsigned i = 0; i < 32; ++i)
                            socket->send_message(data, static_cast<SocketService::Priority>(i % 3));
                        TEST_REQUIRE_EQ(socket->pending_bytes(), std::int64_t(32 * 65536));
                        return;
                    }
                    const std::size_t         size = byte_limit ? 1024 * 1024 : 1;
                    const SocketService::Data data(size, 1);
                    // The callback occupies the strand, so no queue drain can hide admission errors.
                    if (byte_limit) {
                        for (unsigned i = 0; i < 100; ++i)
                            socket->send_message(data, SocketService::Priority::Low);
                    } else {
                        std::vector<std::thread> senders;
                        for (unsigned i = 0; i < 8; ++i)
                            senders.emplace_back([&] {
                                for (unsigned j = 0; j < 300; ++j)
                                    socket->send_message(data, SocketService::Priority::Normal);
                            });
                        for (auto& sender : senders)
                            sender.join();
                    }
                    const auto bulk_limit = byte_limit ? WebSocketService::MaxBulkPendingBytes
                                                       : WebSocketService::MaxBulkPendingMessages;
                    TEST_REQUIRE_EQ(socket->pending_bytes(), static_cast<std::int64_t>(bulk_limit));
                    TEST_REQUIRE(socket->is_active());
                    const auto limit =
                        byte_limit ? WebSocketService::MaxPendingBytes : WebSocketService::MaxPendingMessages;
                    for (std::size_t remaining = limit - bulk_limit; remaining > 0; remaining -= size)
                        socket->send_message(data, SocketService::Priority::High);
                    TEST_REQUIRE_EQ(socket->pending_bytes(), static_cast<std::int64_t>(limit));
                    TEST_REQUIRE(socket->is_active());
                    socket->send_message(data, SocketService::Priority::High);
                    TEST_REQUIRE(socket->is_closed());
                    TEST_REQUIRE_EQ(socket->pending_bytes(), std::int64_t(0));
                    socket->send_message(data, SocketService::Priority::Low);
                    TEST_REQUIRE_EQ(socket->pending_bytes(), std::int64_t(0));
                    checked.set_value();
                } catch (...) {
                    checked.set_exception(std::current_exception());
                }
            };
            connected_promise.set_value(service);
            co_await service->run(false);
        };
        runtime.spawn(connect());
        TEST_REQUIRE(accepted_future.wait_for(5s) == std::future_status::ready);
        TEST_REQUIRE(connected_future.wait_for(5s) == std::future_status::ready);
        auto       accepted  = accepted_future.get();
        auto       connected = connected_future.get();
        const auto status    = checked_future.wait_for(10s);
        if (drain && status == std::future_status::ready) {
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (connected->pending_bytes() != 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(1ms);
        }
        const auto pending = connected->pending_bytes();
        accepted->close_connection();
        connected->close_connection();
        TEST_REQUIRE(accepted->wait_closed(5s));
        TEST_REQUIRE(connected->wait_closed(5s));
        runtime.stop();
        TEST_REQUIRE(status == std::future_status::ready);
        checked_future.get();
        TEST_REQUIRE_EQ(pending, std::int64_t(0));
    }
} // namespace

int main() {
    TEST_REQUIRE(sodium_init() >= 0);
    TestSupport::Runner runner;
    runner.run("queued bytes before strand dispatch", [] {
        check_queue(true);
    });
    runner.run("concurrent queue admission", [] {
        check_queue(false);
    });
    runner.run("queue drain and byte accounting", [] {
        check_queue(false, true);
    });
    return runner.result();
}
