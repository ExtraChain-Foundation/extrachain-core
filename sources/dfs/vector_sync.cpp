#include "dfs/vector_sync.h"

#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "network/network_service.h"
#include "runtime/deadline_task.h"
#include "runtime/runtime.h"

namespace {
    using Clock                            = std::chrono::steady_clock;
    constexpr std::size_t MaxClients       = 32;
    constexpr std::size_t MaxServers       = 16;
    constexpr std::size_t MaxQueuedBytes   = 72ULL * 1024 * 1024;
    constexpr std::size_t MaxMetadataBytes = 256 * 1024;

    bool valid_link(const Dfs::FileLink& link) {
        return !link.owner_id.is_zero() && Dfs::Path::file_path(link.owner_id, link.file_id).has_value();
    }
    bool valid_hex(std::string_view value, bool prefix = false) {
        return (prefix ? value.size() <= 64 : value.size() == 64) && std::ranges::all_of(value, [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    }
    std::string peer(const Responder& responder) {
        return responder.identifiers().size() == 1 ? *responder.identifiers().begin() : std::string { };
    }
    std::string primary(const Dfs::CollectionTemplate& schema) {
        return schema.primary.has_value() ? schema.primary.value().name() : "actor";
    }
} // namespace

struct Dfs::VectorSync::State : std::enable_shared_from_this<State> {
    struct Client {
        FileLink                          link;
        std::string                       peer;
        std::string                       request_id;
        std::string                       snapshot;
        VectorIndexRoot                   root;
        Packets::DfsVectorContentPackage  metadata;
        std::vector<VectorIndexSummary>   todo;
        std::optional<VectorIndexSummary> expected;
        Clock::time_point                 deadline;
    };
    struct Server {
        FileLink                        link;
        std::string                     peer;
        std::unique_ptr<VectorSnapshot> snapshot;
    };
    struct Pending {
        FileLink          link;
        std::string       peer;
        Clock::time_point deadline;
        std::size_t       limit = 0;
    };

    ExtraChain::Core::ExtraChainNode*                 node;
    boost::asio::strand<boost::asio::any_io_executor> strand;
    std::shared_ptr<ExtraChain::Core::DeadlineTask>   timer;
    std::atomic_bool                                  stopped { false };
    std::map<FileLink, Client>                        clients;
    std::map<std::string, Server>                     servers;
    std::uint64_t                                     source_cursor = 0;
    std::map<FileLink, std::string>                   last_sources;
    std::mutex                                        gate;
    std::map<std::string, Pending>                    pending;
    std::map<std::string, unsigned>                   serving;
    std::size_t                                       queued_requests = 0;
    std::set<FileLink>                                starting;
    std::size_t                                       queued_bytes = 0;

    explicit State(ExtraChain::Core::ExtraChainNode* value)
        : node(value)
        , strand(boost::asio::make_strand(value->storage_executor())) {
    }

    template <typename Function>
    void run(Function&& function) {
        ExtraChain::Core::Runtime::guard_handler("vector sync", std::forward<Function>(function))();
        if (!clients.empty() || !servers.empty())
            arm();
    }

    void arm() {
        if (!stopped && !timer->active())
            timer->schedule_after(std::chrono::seconds(1));
    }
    void tick() {
        if (stopped)
            return;
        const auto now = Clock::now();
        std::erase_if(servers, [&](const auto& item) {
            return item.second.snapshot->expired(now);
        });
        std::vector<FileLink> expired;
        for (const auto& [link, client] : clients) {
            if (now >= client.deadline)
                expired.push_back(link);
        }
        for (const auto& link : expired)
            finish(link, false);
        if (!clients.empty() || !servers.empty())
            timer->schedule_after(std::chrono::seconds(1));
    }
    void send(Client& client, const std::string& prefix = { }) {
        Responder target(node->network());
        target.add_identifier(client.peer);
        target            = target.with_new_message_id();
        client.request_id = target.message_id();
        client.deadline   = Clock::now() + std::chrono::seconds(10);
        {
            std::lock_guard lock(gate);
            const auto limit = client.expected.has_value()
                                   ? std::min<std::uint64_t>(MaxQueuedBytes,
                                                             std::min<std::uint64_t>(MaxQueuedBytes,
                                                                                     client.expected.value().bytes)
                                                                     * 3
                                                                 + 64 * 1024)
                                   : MaxMetadataBytes + 4096;
            pending.emplace(client.request_id, Pending { client.link, client.peer, client.deadline, limit });
        }
        node->network()->send_message(VectorSyncRequest { client.link, client.snapshot, prefix },
                                      MessageType::DfsVectorSyncRequest,
                                      SendMode::Focused,
                                      MessageStatus::Request,
                                      target);
        arm();
    }
    void begin(const FileLink& link, const std::string& preferred) {
        if (stopped || clients.contains(link) || clients.size() >= MaxClients)
            return;
        const auto metadata =
            Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->dirs_manager().get_db_instance(),
                                                      link.owner_id,
                                                      link.file_id);
        if (!metadata.has_value() || metadata.value().state == FileState::Removed
            || (metadata.value().type != FileType::Vector && metadata.value().type != FileType::Dictionary))
            return;
        auto peers = node->network()->active_connection_identifiers();
        if (peers.empty())
            return;
        std::ranges::sort(peers);
        auto source = std::ranges::find(peers, preferred);
        if (source == peers.end()) {
            const auto previous = last_sources.find(link);
            const auto last =
                previous == last_sources.end() ? peers.end() : std::ranges::find(peers, previous->second);
            source = last == peers.end() ? peers.begin() + source_cursor++ % peers.size()
                                         : peers.begin() + (last - peers.begin() + 1) % peers.size();
        }
        if (!last_sources.contains(link) && last_sources.size() >= 4096)
            last_sources.erase(last_sources.begin());
        last_sources[link] = *source;
        auto& client       = clients[link];
        client.link        = link;
        client.peer        = *source;
        send(client);
    }
    void finish(FileLink link, bool complete) {
        const auto found = clients.find(link);
        if (found == clients.end())
            return;
        auto client = std::move(found->second);
        clients.erase(found);
        {
            std::lock_guard lock(gate);
            pending.erase(client.request_id);
        }
        if (!client.snapshot.empty()) {
            Responder target(node->network());
            target.add_identifier(client.peer);
            node->network()->send_message(VectorSyncRequest { link, client.snapshot, { }, true },
                                          MessageType::DfsVectorSyncRequest,
                                          SendMode::Focused,
                                          MessageStatus::Request,
                                          target.with_new_message_id());
        }
        if (complete) {
            node->dfs()->network_response_content_vector(client.metadata);
        } else {
            const auto weak = weak_from_this();
            node->dfs()->schedule_delayed(std::chrono::seconds(5), [weak, link] {
                if (const auto self = weak.lock(); self && !self->stopped) {
                    boost::asio::post(self->strand, [self, link] {
                        self->run([&] {
                            self->begin(link, { });
                        });
                    });
                }
            });
        }
    }
    void next(Client& client) {
        const auto path = Path::file_path(client.link.owner_id, client.link.file_id);
        if (!path.has_value()) {
            finish(client.link, false);
            return;
        }
        DbConnector database(path.value());
        if (!database.open(false)) {
            finish(client.link, false);
            return;
        }
        VectorIndex index(database, primary(client.metadata.vector_template));
        while (!client.todo.empty()) {
            const auto expected = client.todo.back();
            client.todo.pop_back();
            const auto local = index.subtree(expected.prefix);
            if (local.has_value() && local.value() == expected)
                continue;
            client.expected = expected;
            send(client, expected.prefix);
            return;
        }
        finish(client.link, true);
    }
    void response(const Pending& request, const std::string& id, std::string_view data) {
        const auto found = clients.find(request.link);
        if (found == clients.end() || found->second.request_id != id)
            return;
        if (!MessagePack::has_bounded_structure(data, 1100000, 2048, 16)) {
            finish(request.link, false);
            return;
        }
        const auto current =
            Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->dirs_manager().get_db_instance(),
                                                      request.link.owner_id,
                                                      request.link.file_id);
        if (!current.has_value() || current.value().state == FileState::Removed
            || (current.value().type != FileType::Vector && current.value().type != FileType::Dictionary)) {
            finish(request.link, false);
            return;
        }
        auto&      client  = found->second;
        const auto decoded = MessagePack::deserialize<VectorSyncReply>(data);
        if (!decoded.has_value()) {
            finish(request.link, false);
            return;
        }
        const auto& reply = decoded.value();
        if (reply.link != client.link || !valid_hex(reply.snapshot) || !VectorIndex::valid_root(reply.root)) {
            finish(request.link, false);
            return;
        }
        if (client.snapshot.empty()) {
            if (!reply.metadata.has_value() || reply.slice.has_value() || !reply.metadata.value().content.empty()
                || reply.metadata.value().owner_id != client.link.owner_id
                || reply.metadata.value().file_id != client.link.file_id
                || MessagePack::serialize(reply.metadata.value()).size() > MaxMetadataBytes) {
                finish(request.link, false);
                return;
            }
            auto vector = node->dfs()->make_vector(client.link.owner_id, client.link.file_id, true);
            if (!vector.has_value() || !vector.value().second.handle_package(reply.metadata.value())) {
                finish(request.link, false);
                return;
            }
            const auto  path = Path::file_path(client.link.owner_id, client.link.file_id);
            DbConnector database(path.value());
            if (!database.open(false)) {
                finish(request.link, false);
                return;
            }
            VectorIndex index(database, primary(reply.metadata.value().vector_template));
            const auto  local = index.root();
            if (!local.has_value() || local.value().schema != reply.root.schema) {
                finish(request.link, false);
                return;
            }
            client.metadata = reply.metadata.value();
            client.snapshot = reply.snapshot;
            client.root     = reply.root;
            if (reply.root.tree.rows != 0)
                client.todo.push_back(reply.root.tree);
        } else {
            if (reply.snapshot != client.snapshot || reply.root.hash != client.root.hash
                || reply.metadata.has_value() || !reply.slice.has_value() || !client.expected.has_value()
                || !VectorSnapshot::verify(client.expected.value(),
                                           primary(client.metadata.vector_template),
                                           reply.slice.value())) {
                finish(request.link, false);
                return;
            }
            const auto& slice = reply.slice.value();
            if (client.todo.size() + slice.children.size() > 1024) {
                finish(request.link, false);
                return;
            }
            client.todo.insert(client.todo.end(), slice.children.begin(), slice.children.end());
            if (!slice.rows.empty()) {
                auto vector     = node->dfs()->make_vector(client.link.owner_id, client.link.file_id, true);
                auto package    = client.metadata;
                package.content = slice.rows;
                if (!vector.has_value() || !vector.value().second.handle_package(package)) {
                    finish(request.link, false);
                    return;
                }
            }
        }
        client.expected.reset();
        next(client);
    }
    void serve(const VectorSyncRequest& request, const Responder& responder) {
        const auto source = peer(responder);
        if (!request.release && node->network()->connection_pending_bytes(source) > 4 * 1024 * 1024)
            return;
        if (request.snapshot.empty()) {
            if (request.release || !request.prefix.empty() || servers.size() >= MaxServers
                || std::ranges::count_if(servers, [&](const auto& item) {
                       return item.second.peer == source;
                   }) >= 4)
                return;
            auto vector = node->dfs()->make_vector(request.link.owner_id, request.link.file_id);
            if (!vector.has_value() || vector.value().first.state == FileState::Removed)
                return;
            const auto type = vector.value().first.type;
            if (type != FileType::Vector && type != FileType::Dictionary)
                return;
            const auto metadata = vector.value().second.generate_content_package_empty();
            if (!metadata.has_value() || MessagePack::serialize(metadata.value()).size() > MaxMetadataBytes)
                return;
            auto snapshot =
                VectorSnapshot::open(Path::file_path(request.link.owner_id, request.link.file_id).value(),
                                     primary(metadata.value().vector_template));
            if (!snapshot.has_value())
                return;
            const auto      id = Utils::generate_random_hex(64);
            VectorSyncReply reply { .link     = request.link,
                                    .snapshot = id,
                                    .root     = snapshot.value()->root(),
                                    .metadata = metadata.value() };
            servers.emplace(id, Server { request.link, source, std::move(snapshot.value()) });
            responder.send_response(reply,
                                    MessageType::DfsVectorSyncReply,
                                    SendMode::Focused,
                                    MessageStatus::Response);
            arm();
            return;
        }
        const auto found = servers.find(request.snapshot);
        if (found == servers.end() || found->second.peer != source || found->second.link != request.link)
            return;
        const auto current =
            Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->dirs_manager().get_db_instance(),
                                                      request.link.owner_id,
                                                      request.link.file_id);
        if (request.release || found->second.snapshot->expired(Clock::now()) || !current.has_value()
            || current.value().state == FileState::Removed) {
            servers.erase(found);
            return;
        }
        const auto slice = found->second.snapshot->read(request.prefix);
        if (!slice.has_value())
            return;
        responder.send_response(VectorSyncReply { .link     = request.link,
                                                  .snapshot = request.snapshot,
                                                  .root     = found->second.snapshot->root(),
                                                  .slice    = slice.value() },
                                MessageType::DfsVectorSyncReply,
                                SendMode::Focused,
                                MessageStatus::Response);
    }
};

Dfs::VectorSync::VectorSync(ExtraChain::Core::ExtraChainNode* node)
    : state_(std::make_shared<State>(node)) {
    const auto weak = std::weak_ptr<State>(state_);
    state_->timer   = ExtraChain::Core::DeadlineTask::create(state_->strand, [weak] {
        if (const auto self = weak.lock())
            self->run([&] {
                self->tick();
            });
    });
}

Dfs::VectorSync::~VectorSync() {
    stop();
}

void Dfs::VectorSync::stop() {
    const auto self = state_;
    if (self->stopped.exchange(true))
        return;
    self->timer->cancel();
    {
        std::lock_guard lock(self->gate);
        self->pending.clear();
    }
    boost::asio::post(self->strand, [self] {
        self->clients.clear();
        self->servers.clear();
    });
}

void Dfs::VectorSync::request(const FileLink& link, const std::string& preferred_peer) {
    const auto self = state_;
    if (self->stopped || !valid_link(link))
        return;
    {
        std::lock_guard lock(self->gate);
        if (self->starting.size() >= MaxClients || !self->starting.insert(link).second)
            return;
    }
    boost::asio::post(self->strand, [self, link, preferred_peer] {
        self->run([&] {
            self->begin(link, preferred_peer);
        });
        std::lock_guard lock(self->gate);
        self->starting.erase(link);
    });
}

bool Dfs::VectorSync::receive_request(std::string_view data, const Responder& responder) {
    const auto self   = state_;
    const auto source = peer(responder);
    if (self->stopped || data.size() > 2048 || !valid_hex(source) || responder.message_id().empty()
        || responder.message_id().size() > 128)
        return false;
    if (!MessagePack::has_bounded_structure(data, 4096, 2048, 16))
        return false;
    const auto request = MessagePack::deserialize<VectorSyncRequest>(data);
    if (!request.has_value() || !valid_link(request.value().link) || !valid_hex(request.value().prefix, true)
        || (!request.value().snapshot.empty() && !valid_hex(request.value().snapshot)))
        return false;
    {
        std::lock_guard lock(self->gate);
        const auto      found = self->serving.find(source);
        if (self->queued_requests >= MaxServers || (found != self->serving.end() && found->second >= 4))
            return false;
        ++self->serving[source];
        ++self->queued_requests;
    }
    boost::asio::post(self->strand, [self, request = request.value(), responder, source] {
        if (!self->stopped)
            self->run([&] {
                self->serve(request, responder);
            });
        std::lock_guard lock(self->gate);
        if (--self->serving.at(source) == 0)
            self->serving.erase(source);
        --self->queued_requests;
    });
    return true;
}

bool Dfs::VectorSync::receive_reply(std::string_view data, const Responder& responder) {
    const auto self   = state_;
    const auto source = peer(responder);
    if (self->stopped || data.size() > MaxQueuedBytes)
        return false;
    State::Pending request;
    {
        std::lock_guard lock(self->gate);
        const auto      found = self->pending.find(responder.message_id());
        if (found == self->pending.end() || found->second.peer != source || Clock::now() >= found->second.deadline
            || data.size() > found->second.limit || data.size() > MaxQueuedBytes - self->queued_bytes)
            return false;
        request = found->second;
        self->pending.erase(found);
        self->queued_bytes += data.size();
    }
    boost::asio::post(self->strand, [self, request, id = responder.message_id(), data = std::string(data)] {
        if (!self->stopped)
            self->run([&] {
                self->response(request, id, data);
            });
        std::lock_guard lock(self->gate);
        self->queued_bytes -= data.size();
    });
    return true;
}
