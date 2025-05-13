#ifndef THREAD_POOL_BOOST_H
#define THREAD_POOL_BOOST_H

#ifdef _WIN32
    #    include <winsock2.h>
#endif

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/dispatch.hpp>

class ThreadPoolBoost {
public:
    ThreadPoolBoost() = delete;

    static std::shared_ptr<ThreadPoolBoost>
    instance(size_t threadsCount = 1);

    static void
    terminate();

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void()) NullaryToken>
    auto
    post(NullaryToken&& nullary_token)
    {
        return boost::asio::post(*m_thread_pool, nullary_token);
    }

    template<BOOST_ASIO_COMPLETION_TOKEN_FOR(void()) NullaryToken>
    auto
    dispatch(NullaryToken&& nullary_token)
    {
        return boost::asio::dispatch(*m_thread_pool, nullary_token);
    }

    void
    join();
private:
    ThreadPoolBoost(size_t threads_count);

    static void
    initialize(boost::asio::thread_pool& thread_pool, const size_t threads_count);

    std::unique_ptr<boost::asio::thread_pool> m_thread_pool;
};

#endif // THREAD_POOL_BOOST_H
