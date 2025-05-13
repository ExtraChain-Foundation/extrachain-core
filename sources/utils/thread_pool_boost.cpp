#include "utils/thread_pool_boost.h"
#include <boost/smart_ptr/detail/spinlock.hpp>

//[CDS] Maybe we will need it for future
// #include <cds/threading/model.h>

static std::shared_ptr<ThreadPoolBoost> threadPool;
static boost::detail::spinlock     mutex;

ThreadPoolBoost::ThreadPoolBoost(size_t threads_count)
{
    if (threads_count == 0)
        throw std::runtime_error("ThreadPoolBoost::ThreadPoolBoost: Incorrect threads count value: " + std::to_string(threads_count));

    m_thread_pool = std::make_unique<boost::asio::thread_pool>(threads_count);
    ThreadPoolBoost::initialize(*m_thread_pool, threads_count);
}

void
ThreadPoolBoost::initialize(boost::asio::thread_pool& pool, const size_t threadsCount)
{
    //[CDS] Maybe we will need it for future
    // std::vector<std::promise<void>> promises(threadsCount);
    // std::atomic<size_t>             counter(0);
    // auto                            attachFunc = [&counter, &promises, threadsCount](const size_t index)
    // {
    //     counter++;
    //     while (counter != threadsCount)
    //         std::this_thread::yield();


    //     cds::threading::Manager::attachThread();
    //     promises[index].set_value();
    // };

    // for (size_t i = 0; i < threadsCount; i++)
    //     boost::asio::post(pool, std::bind(attachFunc, i));

    // for (auto& item : promises)
    //     item.get_future().wait();
}

std::shared_ptr<ThreadPoolBoost>
ThreadPoolBoost::instance(const size_t threads_count)
{
    boost::detail::spinlock::scoped_lock lock(mutex);
    if (!threadPool)
        threadPool = std::shared_ptr<ThreadPoolBoost>(new ThreadPoolBoost(threads_count));

    return threadPool;
}

void
ThreadPoolBoost::terminate()
{
    boost::detail::spinlock::scoped_lock lock(mutex);
    threadPool.reset();
}

void
ThreadPoolBoost::join()
{
    if (m_thread_pool)
        m_thread_pool->join();
}
