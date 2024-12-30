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
#include <set>

#include "utils/exc_logs.h"

class ThreadPool {
private:
    ThreadPool()  = default;
    ~ThreadPool() = default;

public:
    template <class Worker>
    static QThread *addThread(Worker *worker, QThread *newThread = nullptr) {
        QThread *thread = newThread == nullptr ? new QThread() : newThread;

        QObject::connect(thread, &QThread::started, worker, &Worker::process);
        QObject::connect(worker, &Worker::finished, thread, &QThread::quit);
        // QObject::connect(thread, &QThread::finished, worker, &Worker::deleteLater);
        // QObject::connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        QObject::connect(thread, &QThread::finished, [thread, worker]() {
            if (!threads.contains(thread)) {
                eLog("[ThreadPool] Ignore", fmt::ptr(worker));
                return;
            }
            // eLog("[ThreadPool] Remove thread for {}", worker);
            // eLog("[ThreadPool] Remove thread {} for {} {} to {}", thread, worker, //
            // threads.removeAll(thread)
            //, threads.length());
            if (worker)
                worker->deleteLater();
            if (thread)
                thread->deleteLater();
        });

        if (isFirst) {
            eLog("[ThreadPool] Connected with qApp");
            QObject::connect(qApp, &QCoreApplication::aboutToQuit, []() {
                eLog("[ThreadPool] Threads count: {}", threads.size());

                for (QThread *thread : threads) {
                    eLog("[ThreadPool] Remove thread {}", fmt::ptr(thread));
                    thread->quit();
                    thread->wait(2000);
                }
                threads.clear();
            });

            isFirst = false;
        }

        // eLog("[ThreadPool] Move for {}", worker);
        // eLog("[ThreadPool] Move to thread {} for {} {}", thread, worker, threads.length());
        worker->moveToThread(thread);

        if (!thread->isRunning()) {
            // eLog("[ThreadPool] Start {}", thread);
            threads.insert(thread);
            thread->start();
        } else {
            // eLog("[ThreadPool] Ignore start {}", thread);
        }

        return thread;
    }

private:
    static bool                isFirst;
    static std::set<QThread *> threads;
};
