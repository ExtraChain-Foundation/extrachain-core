/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <QCoreApplication>
#include <QThread>

#include <mutex>
#include <set>
#include <vector>

// Compatibility helper for Qt clients. Core does not compile or use this type.
class ThreadPool {
public:
    template <class Worker>
    static QThread *add_thread(Worker *worker, QThread *thread = nullptr) {
        if (worker == nullptr) {
            return nullptr;
        }

        auto *worker_thread = thread != nullptr ? thread : new QThread();
        QObject::connect(worker_thread, &QThread::started, worker, &Worker::process);
        QObject::connect(worker, &Worker::finished, worker_thread, &QThread::quit);
        QObject::connect(worker_thread, &QThread::finished, worker_thread, [worker_thread]() {
            {
                std::scoped_lock lock(mutex());
                threads().erase(worker_thread);
            }
            worker_thread->deleteLater();
        });

        connect_shutdown_once();
        worker->moveToThread(worker_thread);
        if (!worker_thread->isRunning()) {
            std::scoped_lock lock(mutex());
            threads().insert(worker_thread);
            worker_thread->start();
        }
        return worker_thread;
    }

private:
    static std::set<QThread *> &threads() {
        static std::set<QThread *> value;
        return value;
    }

    static std::mutex &mutex() {
        static std::mutex value;
        return value;
    }

    static void connect_shutdown_once() {
        auto *application = QCoreApplication::instance();
        if (application == nullptr) {
            return;
        }
        static std::once_flag once;
        std::call_once(once, [application]() {
            QObject::connect(application, &QCoreApplication::aboutToQuit, []() {
                std::vector<QThread *> active_threads;
                {
                    std::scoped_lock lock(mutex());
                    active_threads.assign(threads().begin(), threads().end());
                }
                for (auto *thread : active_threads) {
                    thread->quit();
                    thread->wait(2000);
                }
            });
        });
    }
};
