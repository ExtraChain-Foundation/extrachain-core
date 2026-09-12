#include "runtime/work_budget.h"

struct ExtraChain::Core::WorkBudget::State {
    struct Usage {
        std::size_t bytes = 0;
        std::size_t jobs  = 0;
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
    --state_->total.jobs;
    const auto peer = state_->peers.find(peer_);
    peer->second.bytes -= bytes_;
    if (--peer->second.jobs == 0)
        state_->peers.erase(peer);
}

bool ExtraChain::Core::WorkBudget::Ticket::stopped() const {
    return state_->stopped.load();
}

ExtraChain::Core::WorkBudget::WorkBudget(Limits limits)
    : state_(std::make_shared<State>(limits)) {
}

std::shared_ptr<ExtraChain::Core::WorkBudget::Ticket> ExtraChain::Core::WorkBudget::reserve(std::string_view peer,
                                                                                            std::size_t bytes) {
    if (peer.size() > 64 || bytes > state_->limits.bytes || bytes > state_->limits.peer_bytes)
        return { };
    const auto      ticket = std::shared_ptr<Ticket>(new Ticket(state_, std::string(peer), bytes));
    std::lock_guard lock(state_->mutex);
    const auto      found = state_->peers.find(ticket->peer_);
    if (state_->stopped || state_->total.jobs >= state_->limits.jobs
        || bytes > state_->limits.bytes - state_->total.bytes)
        return { };
    if (found != state_->peers.end()
        && (found->second.jobs >= state_->limits.peer_jobs
            || bytes > state_->limits.peer_bytes - found->second.bytes))
        return { };
    if (state_->limits.peer_jobs == 0)
        return { };
    auto& usage = state_->peers[ticket->peer_];
    ++usage.jobs;
    usage.bytes += bytes;
    ++state_->total.jobs;
    state_->total.bytes += bytes;
    ticket->active_ = true;
    return ticket;
}

void ExtraChain::Core::WorkBudget::stop() {
    state_->stopped.store(true);
}
