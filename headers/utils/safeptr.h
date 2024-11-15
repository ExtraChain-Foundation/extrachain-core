/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#pragma once

#include <mutex>
#include <memory>
#include <type_traits>

template<typename Type, class Mutex = std::recursive_mutex, class Lock = std::lock_guard<Mutex>>
class SafePtr {
private:
    class AutoLock final
    {
    private:
        Lock                  m_lock;
        std::shared_ptr<Type> m_ptr;

    public:
        AutoLock(std::shared_ptr<Type> ptr, std::shared_ptr<Mutex> mutex) noexcept
            : m_lock(*mutex)
            , m_ptr(ptr)
        {
        }

        Type* operator->() noexcept
        {
            return m_ptr.get();
        }

        const Type* operator->() const noexcept
        {
            return m_ptr.get();
        }
    };

    class AutoLockObj final
    {
    private:
        Lock                  m_lock;
        std::shared_ptr<Type> m_ptr;

    public:
        AutoLockObj(std::shared_ptr<Type> ptr, std::shared_ptr<Mutex> mutex) noexcept
            : m_lock(*mutex)
            , m_ptr(ptr)
        {
        }

        Type& operator*() noexcept
        {
            return *m_ptr;
        }

        Type* operator->() noexcept
        {
            return m_ptr.get();
        }

        const Type* operator->() const noexcept
        {
            return m_ptr.get();
        }
    };

public:
    SafePtr(const SafePtr& other)
    {
        Lock lock(*other.m_mutex);
        m_mutex = other.m_mutex;
        m_ptr   = other.m_ptr;
    }

    SafePtr(SafePtr&&) = default;

    SafePtr& operator=(const SafePtr& other)
    {
        Lock lockMe(*m_mutex);
        Lock lockOther(*other.m_mutex);

        m_ptr   = other.m_ptr;
        m_mutex = other.m_mutex;
        return *this;
    }

    SafePtr& operator=(SafePtr&&) = default;

    template<typename... Args>
    SafePtr(Args... args)
        : m_ptr(std::make_shared<Type>(args...))
        , m_mutex(std::make_shared<Mutex>())
    {
    }

    explicit SafePtr(std::unique_ptr<Type>&& from)
        : m_ptr(std::shared_ptr<Type>(std::move(from)))
        , m_mutex(std::make_shared<Mutex>())
    {
    }

    explicit SafePtr(std::shared_ptr<Type> obj)
        : m_ptr(obj)
        , m_mutex(std::make_shared<Mutex>())
    {
    }

    SafePtr& operator=(std::shared_ptr<Type> from) noexcept
    {
        Lock lock(*m_mutex);
        m_ptr = from;
        return *this;
    }

    SafePtr& operator=(std::unique_ptr<Type>&& from) noexcept
    {
        Lock lock(*m_mutex);
        m_ptr = std::move(from);
        return *this;
    }

    AutoLock operator->() noexcept
    {
        return AutoLock(m_ptr, m_mutex);
    }

    AutoLockObj operator*() noexcept
    {
        return AutoLockObj(m_ptr, m_mutex);
    }

    const AutoLockObj operator*() const noexcept
    {
        return AutoLockObj(m_ptr, m_mutex);
    }

    const AutoLock operator->() const noexcept
    {
        return AutoLock(m_ptr, m_mutex);
    }

    bool operator!() const noexcept
    {
        return !m_ptr;
    }

private:
    void lock()
    {
        m_mutex->lock();
    }

    void unlock()
    {
        m_mutex->unlock();
    }

    std::shared_ptr<Type>  m_ptr;
    std::shared_ptr<Mutex> m_mutex;
};
