#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "extrachain_global.h"

namespace ExtraChain::Core {
    class EXTRACHAIN_EXPORT WorkBudget {
        struct State;

    public:
        struct Limits {
            std::size_t bytes;
            std::size_t jobs;
            std::size_t peer_bytes;
            std::size_t peer_jobs;
        };

        class EXTRACHAIN_EXPORT Ticket {
        public:
            ~Ticket();
            Ticket(const Ticket&)            = delete;
            Ticket& operator=(const Ticket&) = delete;
            bool    stopped() const;

        private:
            friend class WorkBudget;
            Ticket(std::shared_ptr<State> state, std::string peer, std::size_t bytes);
            std::shared_ptr<State> state_;
            std::string            peer_;
            std::size_t            bytes_;
            bool                   active_ = false;
        };

        explicit WorkBudget(Limits limits);
        std::shared_ptr<Ticket> reserve(std::string_view peer, std::size_t bytes);
        void                    stop();

    private:
        std::shared_ptr<State> state_;
    };
} // namespace ExtraChain::Core
