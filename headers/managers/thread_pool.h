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

#pragma once

#include <QCoreApplication>
#include <QThread>
#include <mutex>
#include <set>

#include "utils/exc_logs.h"

class ThreadPool {
private:
    ThreadPool()  = default;
    ~ThreadPool() = default;

public:
    template <class Worker>
    static QThread *add_thread(Worker *worker, QThread *newThread = nullptr) {
        QThread *thread = newThread == nullptr ? new QThread() : newThread;

        QObject::connect(thread, &QThread::started, worker, &Worker::process);
        QObject::connect(worker, &Worker::finished, thread, &QThread::quit);
        QObject::connect(worker, &Worker::finished, worker, &Worker::deleteLater);
        QObject::connect(thread, &QThread::finished, [thread, worker]() {
            {
                std::scoped_lock lock(threads_mutex);
                if (threads.erase(thread) == 0) {
                    eLog("[ThreadPool] Ignore {}", fmt::ptr(worker));
                    return;
                }
            }
            if (thread)
                thread->deleteLater();
        });

        {
            std::scoped_lock lock(threads_mutex);
            if (!shutdown_connected) {
                eLog("[ThreadPool] Connected with qApp");
                QObject::connect(qApp, &QCoreApplication::aboutToQuit, []() {
                    std::set<QThread *> active_threads;
                    {
                        std::scoped_lock lock(threads_mutex);
                        active_threads.swap(threads);
                    }

                    eLog("[ThreadPool] Threads count: {}", active_threads.size());

                    for (QThread *thread : active_threads) {
                        eLog("[ThreadPool] Remove thread {}", fmt::ptr(thread));
                        thread->quit();
                        thread->wait(2000);
                    }
                });

                shutdown_connected = true;
            }
        }

        // eLog("[ThreadPool] Move for {}", worker);
        // eLog("[ThreadPool] Move to thread {} for {} {}", thread, worker, threads.length());
        worker->moveToThread(thread);

        if (!thread->isRunning()) {
            {
                std::scoped_lock lock(threads_mutex);
                threads.insert(thread);
            }
            thread->start();
        } else {
            // eLog("[ThreadPool] Ignore start {}", thread);
        }

        return thread;
    }

private:
    static bool                shutdown_connected;
    static std::mutex          threads_mutex;
    static std::set<QThread *> threads;
};
