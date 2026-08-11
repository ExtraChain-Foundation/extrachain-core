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
#include "utils/thread_pool_boost.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {

    enum class PoolKind : std::size_t {
        Dfs,
        General,
        DagSync,
        Dag,
        Count
    };

    constexpr auto pool_count = static_cast<std::size_t>(PoolKind::Count);

    struct PoolRegistry {
        std::mutex                                               mutex;
        bool                                                     terminating = false;
        std::array<std::shared_ptr<ThreadPoolBoost>, pool_count> pools;
    };

    PoolRegistry& pool_registry() {
        static PoolRegistry registry;
        return registry;
    }

    std::shared_ptr<ThreadPoolBoost>& pool_at(PoolRegistry& registry, PoolKind kind) {
        return registry.pools[static_cast<std::size_t>(kind)];
    }

    template <typename Factory>
    std::shared_ptr<ThreadPoolBoost> get_pool(PoolKind kind, Factory&& factory) {
        auto&            registry = pool_registry();
        std::scoped_lock lock(registry.mutex);
        auto&            pool = pool_at(registry, kind);
        if (!pool) {
            pool = std::forward<Factory>(factory)();
        }
        return pool;
    }

} // namespace

ThreadPoolBoost::ThreadPoolBoost(std::size_t threads_count) {
    if (threads_count == 0) {
        throw std::runtime_error("ThreadPoolBoost::ThreadPoolBoost: Incorrect threads count value: "
                                 + std::to_string(threads_count));
    }

    m_thread_pool = std::make_unique<boost::asio::thread_pool>(threads_count);
}

std::shared_ptr<ThreadPoolBoost> ThreadPoolBoost::instance_dfs(const std::size_t threads_count) {
    return get_pool(PoolKind::Dfs, [threads_count] {
        return std::shared_ptr<ThreadPoolBoost>(new ThreadPoolBoost(threads_count));
    });
}

std::shared_ptr<ThreadPoolBoost> ThreadPoolBoost::instance_dag(std::size_t threads_count) {
    return get_pool(PoolKind::Dag, [threads_count] {
        return std::shared_ptr<ThreadPoolBoost>(new ThreadPoolBoost(threads_count));
    });
}

std::shared_ptr<ThreadPoolBoost> ThreadPoolBoost::instance_dag_sync(std::size_t threads_count) {
    return get_pool(PoolKind::DagSync, [threads_count] {
        return std::shared_ptr<ThreadPoolBoost>(new ThreadPoolBoost(threads_count));
    });
}

std::shared_ptr<ThreadPoolBoost> ThreadPoolBoost::instance(std::size_t threads_count) {
    return get_pool(PoolKind::General, [threads_count] {
        return std::shared_ptr<ThreadPoolBoost>(new ThreadPoolBoost(threads_count));
    });
}

void ThreadPoolBoost::terminate() {
    auto& registry = pool_registry();
    {
        std::scoped_lock lock(registry.mutex);
        if (registry.terminating) {
            return;
        }
        registry.terminating = true;
    }

    for (;;) {
        std::array<std::shared_ptr<ThreadPoolBoost>, pool_count> pools;
        {
            std::scoped_lock lock(registry.mutex);
            if (std::ranges::all_of(registry.pools, [](const auto& pool) {
                    return !pool;
                })) {
                registry.terminating = false;
                break;
            }
            pools.swap(registry.pools);
        }

        for (const auto& pool : pools) {
            if (pool && pool->m_thread_pool) {
                pool->m_thread_pool->stop();
            }
        }
        for (const auto& pool : pools) {
            if (pool && pool->m_thread_pool) {
                pool->m_thread_pool->join();
            }
        }
    }
}

void ThreadPoolBoost::join() {
    if (m_thread_pool) {
        m_thread_pool->join();
    }
}
