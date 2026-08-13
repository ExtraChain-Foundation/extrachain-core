#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <boost/asio/post.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

#include "network/network_runtime.h"
#include "network/responder.h"
#include "core/extrachain_node.h"
#include "network/network_status.h"
#include "network/traffic_meter.h"
#include "runtime/event.h"
#include "runtime/deadline_task.h"
#include "runtime/periodic_task.h"
#include "runtime/runtime.h"
#include "utils/thread_pool_boost.h"
#include "utils/version.h"

namespace {
    void require(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    class TestResponseSender final : public ResponseSender {
    public:
        std::string send_response(const std::string& data_serialized,
                                  MessageType        type,
                                  SendMode           send_mode,
                                  MessageStatus      status,
                                  const Responder&   responder) override {
            data_       = data_serialized;
            type_       = type;
            send_mode_  = send_mode;
            status_     = status;
            message_id_ = responder.message_id();
            return "response-sent";
        }

        std::string   data_;
        MessageType   type_      = MessageType::Unknown;
        SendMode      send_mode_ = SendMode::Neighbours;
        MessageStatus status_    = MessageStatus::NoStatus;
        std::string   message_id_;
    };
} // namespace

int main() {
    using namespace std::chrono_literals;
    using ExtraChain::Core::DeadlineTask;
    using ExtraChain::Core::Event;
    using ExtraChain::Core::NetworkRuntime;
    using ExtraChain::Core::NetworkStatus;
    using ExtraChain::Core::PeriodicTask;
    using ExtraChain::Core::Runtime;
    using ExtraChain::Core::TrafficMeter;

    TestResponseSender response_sender;
    Responder          responder(&response_sender);
    responder.set_message_id("0123456789abcde");
    require(responder.send_response(std::string("payload"),
                                    MessageType::Custom,
                                    SendMode::Focused,
                                    MessageStatus::Response)
                == "response-sent",
            "responder must route a response through the Qt-free sender interface");
    const auto response_payload = MessagePack::deserialize<std::string>(response_sender.data_);
    require(response_payload.has_value() && response_payload.value() == "payload",
            "responder must preserve the serialized response payload");
    require(response_sender.type_ == MessageType::Custom && response_sender.send_mode_ == SendMode::Focused
                && response_sender.status_ == MessageStatus::Response
                && response_sender.message_id_ == "0123456789abcde",
            "responder must preserve response routing data");

    ExtraChain::Core::ExtraChainNode light_node(false, false, 0, RuntimeProfile::MobileLight);
    require(light_node.runtime_profile() == RuntimeProfile::MobileLight,
            "core node must keep its runtime profile");
    require(light_node.runtime_limits().peer_limit == 3, "foreground mobile node must use the mobile peer limit");
    std::atomic<RuntimeActivity> observed_activity { RuntimeActivity::Foreground };
    std::atomic_int              activity_events { 0 };
    auto activity_connection = light_node.runtime_activity_event().subscribe([&](RuntimeActivity activity) {
        observed_activity = activity;
        activity_events.fetch_add(1, std::memory_order_release);
    });
    light_node.set_runtime_activity(RuntimeActivity::Background);
    const auto activity_deadline = std::chrono::steady_clock::now() + 2s;
    while (activity_events.load(std::memory_order_acquire) == 0
           && std::chrono::steady_clock::now() < activity_deadline) {
        std::this_thread::sleep_for(1ms);
    }
    require(observed_activity.load(std::memory_order_acquire) == RuntimeActivity::Background,
            "core node must publish an activity change");
    require(light_node.runtime_limits().dfs_downloads == 0, "background mobile node must stop DFS downloads");
    light_node.set_runtime_activity(RuntimeActivity::Background);
    std::this_thread::sleep_for(20ms);
    require(activity_events.load(std::memory_order_acquire) == 1, "core node must ignore a duplicate activity");

    std::atomic_int serial_active { 0 };
    std::atomic_int serial_peak { 0 };
    std::atomic_int serial_complete { 0 };
    for (int index = 0; index < 32; ++index) {
        boost::asio::post(light_node.serial_executor(), [&] {
            const int active = serial_active.fetch_add(1, std::memory_order_acq_rel) + 1;
            int       peak   = serial_peak.load(std::memory_order_acquire);
            while (peak < active && !serial_peak.compare_exchange_weak(peak, active, std::memory_order_acq_rel)) {
            }
            std::this_thread::sleep_for(1ms);
            serial_active.fetch_sub(1, std::memory_order_acq_rel);
            serial_complete.fetch_add(1, std::memory_order_acq_rel);
        });
    }
    const auto serial_deadline = std::chrono::steady_clock::now() + 2s;
    while (serial_complete.load(std::memory_order_acquire) != 32
           && std::chrono::steady_clock::now() < serial_deadline) {
        std::this_thread::sleep_for(1ms);
    }
    require(serial_complete.load(std::memory_order_acquire) == 32,
            "core node serial executor must complete accepted work");
    require(serial_peak.load(std::memory_order_acquire) == 1,
            "core node serial executor must not run node work in parallel");

    require(Utils::compare_versions("0.25.0", "0.26.0") == Utils::VersionCompareResult::Newer,
            "version comparison must detect a newer peer");
    require(Utils::compare_versions("0.26", "0.26.0") == Utils::VersionCompareResult::Same,
            "version comparison must treat missing components as zero");
    require(Utils::compare_versions("0.26.invalid", "0.26.0") == Utils::VersionCompareResult::Same,
            "version comparison must preserve invalid-component compatibility");

    Event<int> event;
    int        observed = 0;
    {
        auto connection = event.subscribe([&observed](int value) {
            observed += value;
        });
        event.publish(4);
        require(observed == 4, "event must call a connected subscriber");
    }
    event.publish(4);
    require(observed == 4, "scoped event connection must disconnect on destruction");

    NetworkStatus         network_status;
    NetworkStatus::Status observed_status   = NetworkStatus::Status::Unknown;
    auto                  status_connection = network_status.subscribe([&](NetworkStatus::Status status) {
        observed_status = status;
    });
    require(network_status.status() == NetworkStatus::Status::Offline,
            "network status must start as offline until a platform adapter updates it");
    require(network_status.update(NetworkStatus::Status::Local), "network status must report a state change");
    require(observed_status == NetworkStatus::Status::Local, "network status must publish the changed state");
    require(!network_status.update(NetworkStatus::Status::Local), "network status must ignore a duplicate state");

    auto* traffic_meter = TrafficMeter::get_instance();
    traffic_meter->add_bytes_sent("runtime-test-peer", 17);
    traffic_meter->add_bytes_received("runtime-test-peer", 23);
    require(traffic_meter->total_bytes_sent_from_connection("runtime-test-peer") == 17,
            "traffic meter must keep the sent count for a peer");
    require(traffic_meter->total_bytes_received_from_connection("runtime-test-peer") == 23,
            "traffic meter must keep the received count for a peer");
    require(traffic_meter->total_bytes() == std::pair<std::uint64_t, std::uint64_t> { 17, 23 },
            "traffic meter must keep constant-time total counters");

    Runtime                 runtime({ .io_threads = 2, .blocking_threads = 1 });
    std::mutex              mutex;
    std::condition_variable condition;
    std::atomic_int         ticks { 0 };

    const auto timer = PeriodicTask::create(runtime.executor(), 5ms, [&] {
        if (ticks.fetch_add(1, std::memory_order_acq_rel) + 1 >= 3) {
            condition.notify_one();
        }
    });

    runtime.start();
    timer->start();

    std::atomic_bool blocking_complete { false };
    std::atomic_bool blocking_exception_caught { false };
    std::thread::id  blocking_thread;
    boost::asio::co_spawn(
        runtime.executor(),
        [&]() -> boost::asio::awaitable<void> {
            const auto answer = co_await runtime.async_blocking([&]() {
                blocking_thread = std::this_thread::get_id();
                return 42;
            });
            require(answer == 42, "blocking work must return its result");
            require(std::this_thread::get_id() != blocking_thread,
                    "blocking work must return to the I/O executor");
            try {
                co_await runtime.async_blocking([]() -> void {
                    throw std::runtime_error("expected runtime test error");
                });
            } catch (const std::runtime_error&) {
                blocking_exception_caught.store(true, std::memory_order_release);
            }
            blocking_complete.store(true, std::memory_order_release);
            condition.notify_one();
        },
        boost::asio::detached);

    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return blocking_complete.load(std::memory_order_acquire);
                                   }),
                "blocking work must complete through the coroutine bridge");
    }
    require(blocking_exception_caught.load(std::memory_order_acquire),
            "blocking work must propagate exceptions on the I/O executor");

    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return ticks.load(std::memory_order_acquire) >= 3;
                                   }),
                "periodic task must execute on the runtime");
    }

    timer->stop();
    const int stopped_at = ticks.load(std::memory_order_acquire);
    std::this_thread::sleep_for(30ms);
    require(ticks.load(std::memory_order_acquire) == stopped_at, "stopped task must not execute again");

    timer->start();
    timer->stop();
    timer->start();
    const int restarted_at = ticks.load(std::memory_order_acquire);
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return ticks.load(std::memory_order_acquire) > restarted_at;
                                   }),
                "rapidly restarted task must execute once scheduling settles");
    }
    timer->stop();

    std::atomic_int deadline_hits { 0 };
    const auto      deadline = DeadlineTask::create(runtime.executor(), [&] {
        deadline_hits.fetch_add(1, std::memory_order_acq_rel);
        condition.notify_one();
    });
    deadline->schedule_after(100ms);
    deadline->schedule_earlier(5ms);
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return deadline_hits.load(std::memory_order_acquire) == 1;
                                   }),
                "deadline task must allow an earlier deadline");
    }
    deadline->schedule_after(5ms);
    deadline->cancel();
    std::this_thread::sleep_for(30ms);
    require(deadline_hits.load(std::memory_order_acquire) == 1, "cancelled deadline must not execute");

    runtime.stop();
    require(!runtime.running(), "runtime must report the stopped state");

    Runtime          draining_runtime({ .io_threads = 1, .blocking_threads = 1 });
    std::atomic_bool drained { false };
    const auto       draining_task = DeadlineTask::create(draining_runtime.executor(), [&] {
        drained.store(true, std::memory_order_release);
    });
    draining_runtime.start();
    draining_task->schedule_after(10ms);
    draining_runtime.stop();
    require(drained.load(std::memory_order_acquire), "runtime stop must drain accepted asynchronous work");

    NetworkRuntime  network({ .io_threads = 1, .blocking_threads = 1 });
    std::atomic_int accepted { 0 };
    const auto      listen_port = network.listen(0, [&](NetworkRuntime::Tcp::socket socket) {
        boost::system::error_code ignored;
        socket.close(ignored);
        accepted.fetch_add(1, std::memory_order_acq_rel);
        condition.notify_one();
    });
    require(listen_port.has_value(), "network runtime must bind an ephemeral listener");

    const auto sync_probe = NetworkRuntime::probe("127.0.0.1", listen_port.value(), 1s);
    require(sync_probe.has_value(), "synchronous probe must reach the Boost listener");
    const auto hostname_probe = NetworkRuntime::probe("localhost", listen_port.value(), 1s);
    require(hostname_probe.has_value(), "synchronous probe must resolve a host name");

    boost::asio::io_context       http_context;
    NetworkRuntime::Tcp::acceptor http_acceptor(http_context, { boost::asio::ip::address_v4::loopback(), 0 });
    const auto                    http_port = http_acceptor.local_endpoint().port();
    std::thread                   http_server([&] {
        NetworkRuntime::Tcp::socket socket(http_context);
        boost::system::error_code   error;
        http_acceptor.accept(socket, error);
        if (error) {
            return;
        }
        boost::asio::streambuf request;
        boost::asio::read_until(socket, request, "\r\n\r\n", error);
        if (error) {
            return;
        }
        const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
        boost::asio::write(socket, boost::asio::buffer(response), error);
    });

    std::atomic_bool http_complete { false };
    std::atomic_bool http_ok { false };
    network.async_http_get("127.0.0.1", http_port, "/runtime", 1s, [&](NetworkRuntime::HttpResult result) {
        http_ok.store(result.has_value() && result.value() == "OK", std::memory_order_release);
        http_complete.store(true, std::memory_order_release);
        condition.notify_one();
    });
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return http_complete.load(std::memory_order_acquire);
                                   }),
                "asynchronous HTTP request must complete");
    }
    http_server.join();
    require(http_ok.load(std::memory_order_acquire), "asynchronous HTTP request must return the response body");

    boost::asio::io_context       post_context;
    NetworkRuntime::Tcp::acceptor post_acceptor(post_context, { boost::asio::ip::address_v4::loopback(), 0 });
    const auto                    post_port = post_acceptor.local_endpoint().port();
    std::atomic_bool              post_request_valid { false };
    std::thread                   post_server([&] {
        namespace http = boost::beast::http;

        NetworkRuntime::Tcp::socket socket(post_context);
        boost::system::error_code   error;
        post_acceptor.accept(socket, error);
        if (error) {
            return;
        }
        boost::beast::flat_buffer        buffer;
        http::request<http::string_body> request;
        http::read(socket, buffer, request, error);
        if (error) {
            return;
        }
        post_request_valid.store(request.method() == http::verb::post && request.target() == "/runtime"
                                     && request[http::field::content_type] == "application/json"
                                     && request.body() == "{\"ready\":true}",
                                 std::memory_order_release);
        http::response<http::string_body> response(http::status::ok, 11);
        response.set(http::field::content_type, "text/plain");
        response.body() = "POST-OK";
        response.prepare_payload();
        http::write(socket, response, error);
    });

    std::atomic_bool post_complete { false };
    std::atomic_bool post_ok { false };
    network.async_http_post("127.0.0.1",
                            post_port,
                            "/runtime",
                            "application/json",
                            "{\"ready\":true}",
                            1s,
                            [&](NetworkRuntime::HttpResult result) {
                                post_ok.store(result.has_value() && result.value() == "POST-OK",
                                              std::memory_order_release);
                                post_complete.store(true, std::memory_order_release);
                                condition.notify_one();
                            });
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return post_complete.load(std::memory_order_acquire);
                                   }),
                "asynchronous HTTP POST request must complete");
    }
    post_server.join();
    require(post_ok.load(std::memory_order_acquire), "asynchronous HTTP POST must return the response body");
    require(post_request_valid.load(std::memory_order_acquire),
            "asynchronous HTTP POST must preserve target, content type, and body");

    std::atomic_bool async_probe_ok { false };
    network.async_probe("127.0.0.1", listen_port.value(), 1s, [&](bool connected, std::string) {
        async_probe_ok.store(connected, std::memory_order_release);
        condition.notify_one();
    });
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return async_probe_ok.load(std::memory_order_acquire)
                                              && accepted.load(std::memory_order_acquire) >= 3;
                                   }),
                "asynchronous probe and accept callback must complete");
    }

    boost::asio::io_context       stalled_context;
    NetworkRuntime::Tcp::acceptor stalled_acceptor(stalled_context,
                                                   { boost::asio::ip::address_v4::loopback(), 0 });
    std::atomic_bool              stalled_accepted { false };
    std::atomic_bool              cancelled_request_complete { false };
    std::thread                   stalled_server([&] {
        NetworkRuntime::Tcp::socket socket(stalled_context);
        boost::system::error_code   error;
        stalled_acceptor.accept(socket, error);
        if (!error) {
            stalled_accepted.store(true, std::memory_order_release);
            condition.notify_one();
            std::this_thread::sleep_for(500ms);
        }
    });
    network.async_http_get("127.0.0.1",
                           stalled_acceptor.local_endpoint().port(),
                           "/stalled",
                           5s,
                           [&](NetworkRuntime::HttpResult result) {
                               cancelled_request_complete.store(!result.has_value(), std::memory_order_release);
                               condition.notify_one();
                           });
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return stalled_accepted.load(std::memory_order_acquire);
                                   }),
                "stalled HTTP server must accept the request");
    }
    const auto stop_started = std::chrono::steady_clock::now();
    network.stop();
    const auto stop_duration = std::chrono::steady_clock::now() - stop_started;
    stalled_server.join();
    require(!network.listening(), "stopped network runtime must close its listener");
    require(cancelled_request_complete.load(std::memory_order_acquire),
            "network stop must cancel an active HTTP request");
    require(stop_duration < 1s, "network stop must not wait for an active HTTP timeout");

    std::atomic_int pool_tasks { 0 };
    auto            pool = ThreadPoolBoost::instance(2);
    for (int index = 0; index < 64; ++index) {
        pool->post([&] {
            if (pool_tasks.fetch_add(1, std::memory_order_acq_rel) + 1 == 64) {
                condition.notify_one();
            }
        });
    }
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return pool_tasks.load(std::memory_order_acquire) == 64;
                                   }),
                "shared Boost pool must execute queued tasks");
    }
    ThreadPoolBoost::terminate();

    auto replacement_pool = ThreadPoolBoost::instance(1);
    require(replacement_pool != pool, "shared Boost pool must allow clean creation after termination");
    replacement_pool->post([&] {
        pool_tasks.fetch_add(1, std::memory_order_acq_rel);
        condition.notify_one();
    });
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock,
                                   2s,
                                   [&] {
                                       return pool_tasks.load(std::memory_order_acquire) == 65;
                                   }),
                "replacement Boost pool must execute work");
    }
    ThreadPoolBoost::terminate();

    std::cout << "PASS: runtime, event, timer, listener, probe, and shared pool lifecycle\n";
}
