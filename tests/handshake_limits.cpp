#include <chrono>
#include <filesystem>
#include <future>
#include <thread>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"
#include "utils/exc_utils.h"

namespace asio  = boost::asio;
namespace beast = boost::beast;
using Tcp       = asio::ip::tcp;
using namespace std::chrono_literals;

namespace {
    template <class Predicate>
    bool until(Predicate predicate, std::chrono::milliseconds timeout) {
        const auto end = std::chrono::steady_clock::now() + timeout;
        do {
            if (predicate())
                return true;
            std::this_thread::sleep_for(10ms);
        } while (std::chrono::steady_clock::now() < end);
        return predicate();
    }
    class Pending final : public SocketService {
    public:
        explicit Pending(PeerContext& context)
            : SocketService(context) {
            ip_ = "192.0.2.1";
        }
        bool is_active() const override {
            return false;
        }
        std::string protocol_string() const override {
            return "pending-test";
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
    bool closed(Tcp::socket& socket) {
        boost::system::error_code error;
        char                      byte;
        socket.read_some(asio::buffer(&byte, 1), error);
        return error == asio::error::eof || error == asio::error::connection_reset;
    }
} // namespace

int main(int argc, char** argv) {
    const std::string selected = argc > 1 ? argv[1] : "all";
    const auto        original = std::filesystem::current_path();
    const auto        directory =
        std::filesystem::temp_directory_path() / ("extrachain-handshake-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("handshake-limits", ActorType::User, owner);
    TEST_REQUIRE(until(
        [&] {
            return node->network_runtime().listening();
        },
        5s));
    const auto          port = node->network_runtime().listen({ }, { }).value();
    asio::io_context    io;
    const Tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), port);
    auto                connect = [&] {
        auto socket = std::make_unique<Tcp::socket>(io);
        socket->connect(endpoint);
        socket->non_blocking(true);
        return socket;
    };
    auto pending_count = [&] {
        std::size_t count       = 0;
        auto        connections = *node->network()->connections();
        for (const auto& connection : *connections) {
            count += !connection->is_closed() && !connection->is_active() ? 1 : 0;
        }
        return count;
    };
    if (selected == "all" || selected == "limits") {
        std::vector<std::unique_ptr<Tcp::socket>> stalled;
        for (unsigned i = 0; i < 4; ++i)
            stalled.push_back(connect());
        TEST_REQUIRE(until(
            [&] {
                return pending_count() == 4;
            },
            2s));
        auto rejected = connect();
        TEST_REQUIRE(until(
            [&] {
                return closed(*rejected);
            },
            2s));
        TEST_REQUIRE_EQ(pending_count(), std::size_t(4));
        stalled.clear();
        TEST_REQUIRE(until(
            [&] {
                return pending_count() == 0;
            },
            2s));

        std::vector<std::shared_ptr<Pending>> placeholders;
        for (unsigned i = 0; i < 32; ++i) {
            auto pending = std::make_shared<Pending>(*node->network());
            node->network()->connections()->insert(pending);
            placeholders.push_back(pending);
        }
        rejected = connect();
        TEST_REQUIRE(until(
            [&] {
                return closed(*rejected);
            },
            2s));
        for (const auto& pending : placeholders)
            node->network()->connections()->erase(pending);
        placeholders.clear();
    }
    for (bool exchange_key : { false, true }) {
        if (selected != "all" && selected != (exchange_key ? "handshake-frame" : "key-frame")) {
            continue;
        }
        beast::websocket::stream<beast::tcp_stream> client(io);
        auto                                        result = asio::co_spawn(
            io,
            [&]() -> asio::awaitable<bool> {
                co_await beast::get_lowest_layer(client).async_connect(endpoint, asio::use_awaitable);
                co_await client.async_handshake("127.0.0.1", "/", asio::use_awaitable);
                beast::flat_buffer buffer;
                co_await client.async_read(buffer, asio::use_awaitable);
                buffer.consume(buffer.size());
                if (exchange_key) {
                    const auto keys = Cryptography::asymmetric_create_pair();
                    const auto key  = Utils::to_base64(keys.second);
                    client.text(true);
                    co_await client.async_write(asio::buffer(key), asio::use_awaitable);
                    co_await client.async_read(buffer, asio::use_awaitable);
                    buffer.consume(buffer.size());
                }
                client.text(true);
                const std::string oversized(8193, 'A');
                co_await client.async_write(asio::buffer(oversized), asio::use_awaitable);
                boost::system::error_code error;
                co_await client.async_read(buffer, asio::redirect_error(asio::use_awaitable, error));
                co_return error == beast::websocket::error::closed;
            },
            asio::use_future);
        io.restart();
        io.run_for(3s);
        TEST_REQUIRE(result.wait_for(0s) == std::future_status::ready);
        TEST_REQUIRE(result.get());
        TEST_REQUIRE(until(
            [&] {
                return pending_count() == 0;
            },
            2s));
    }
    if (selected == "all" || selected == "deadline") {
        auto idle = connect();
        TEST_REQUIRE(until(
            [&] {
                return pending_count() == 1;
            },
            2s));
        TEST_REQUIRE(until(
            [&] {
                return closed(*idle);
            },
            12s));
        TEST_REQUIRE(until(
            [&] {
                return pending_count() == 0;
            },
            2s));
    }
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
