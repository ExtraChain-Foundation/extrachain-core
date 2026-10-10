#include "runtime/work_budget.h"

struct ExtraChain::Core::WorkBudget::State {
    struct Usage {
        std::size_t bytes = 0;
        std::size_t jobs  = 0;
        std::size_t waiting = 0;
    };
    explicit State(Limits value)
        : limits(value) {
    }
    Limits                       limits;
    std::atomic_bool             stopped { false };
    std::mutex                   mutex;
    Usage                        total;
    std::map<std::string, Usage> peers;
};

ExtraChain::Core::WorkBudget::Ticket::Ticket(std::shared_ptr<State> state, std::string peer, std::size_t bytes)
    : state_(std::move(state))
    , peer_(std::move(peer))
    , bytes_(bytes) {
}

ExtraChain::Core::WorkBudget::Ticket::~Ticket() {
    if (!active_)
        return;
    std::lock_guard lock(state_->mutex);
    state_->total.bytes -= bytes_;
    const auto peer = state_->peers.find(peer_);
    peer->second.bytes -= bytes_;
    if (started_) {
        --state_->total.jobs;
        --peer->second.jobs;
    } else {
        --state_->total.waiting;
        --peer->second.waiting;
    }
    if (peer->second.jobs == 0 && peer->second.waiting == 0)
        state_->peers.erase(peer);
}

bool ExtraChain::Core::WorkBudget::Ticket::stopped() const {
    return state_->stopped.load();
}

bool ExtraChain::Core::WorkBudget::Ticket::try_start() {
    std::lock_guard lock(state_->mutex);
    if (!active_ || state_->stopped)
        return false;
    if (started_)
        return true;
    auto& peer = state_->peers.at(peer_);
    if (state_->total.jobs >= state_->limits.jobs || peer.jobs >= state_->limits.peer_jobs)
        return false;
    --state_->total.waiting;
    --peer.waiting;
    ++state_->total.jobs;
    ++peer.jobs;
    started_ = true;
    return true;
}

ExtraChain::Core::WorkBudget::WorkBudget(Limits limits)
    : state_(std::make_shared<State>(limits)) {
}

std::shared_ptr<ExtraChain::Core::WorkBudget::Ticket> ExtraChain::Core::WorkBudget::reserve(std::string_view peer,
                                                                                            std::size_t bytes) {
    return reserve_impl(peer, bytes, false);
}

std::shared_ptr<ExtraChain::Core::WorkBudget::Ticket> ExtraChain::Core::WorkBudget::reserve_waiting(
    std::string_view peer,
    std::size_t      bytes) {
    return reserve_impl(peer, bytes, true);
}

std::shared_ptr<ExtraChain::Core::WorkBudget::Ticket> ExtraChain::Core::WorkBudget::reserve_impl(
    std::string_view peer,
    std::size_t      bytes,
    bool             allow_wait) {
    if (peer.size() > 64 || bytes > state_->limits.bytes || bytes > state_->limits.peer_bytes)
        return { };
    const auto      ticket = std::shared_ptr<Ticket>(new Ticket(state_, std::string(peer), bytes));
    std::lock_guard lock(state_->mutex);
    const auto      found = state_->peers.find(ticket->peer_);
    if (state_->stopped || bytes > state_->limits.bytes - state_->total.bytes || state_->limits.peer_jobs == 0)
        return { };
    if (found != state_->peers.end() && bytes > state_->limits.peer_bytes - found->second.bytes)
        return { };
    const bool started = state_->total.jobs < state_->limits.jobs
                         && (found == state_->peers.end()
                             || (found->second.jobs < state_->limits.peer_jobs && found->second.waiting == 0));
    // Retained frames use the same byte budget, with at most one waiting frame per peer.
    if (!started
        && (!allow_wait || state_->total.waiting >= state_->limits.jobs
            || (found != state_->peers.end() && found->second.waiting != 0)))
        return { };
    auto& usage = state_->peers[ticket->peer_];
    usage.bytes += bytes;
    state_->total.bytes += bytes;
    if (started) {
        ++usage.jobs;
        ++state_->total.jobs;
    } else {
        ++usage.waiting;
        ++state_->total.waiting;
    }
    ticket->active_ = true;
    ticket->started_ = started;
    return ticket;
}

void ExtraChain::Core::WorkBudget::stop() {
    state_->stopped.store(true);
}
