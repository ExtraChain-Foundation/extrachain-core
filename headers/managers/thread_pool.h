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

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <QCoreApplication>
#include <QDebug>
#include <QThread>

class ThreadPool {
private:
    ThreadPool() = default;
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
                qDebug() << "[ThreadPool] Ignore" << worker;
                return;
            }
            // qDebug() << "[ThreadPool] Remove thread for" << worker;
            // qDebug() << "[ThreadPool] Remove thread" << thread << "for" << worker <<
            // threads.removeAll(thread)
            //          << "to" << threads.length();
            worker->deleteLater();
            thread->deleteLater();
        });

        if (isFirst) {
            qDebug() << "[ThreadPool] Connected with qApp";
            QObject::connect(qApp, &QCoreApplication::aboutToQuit, []() {
                // qDebug() << "[ThreadPool] Remove all threads" << threads.length() << threads;
                // for (auto i = threads.size() - 1; i != 0; i--)
                //     threads[i]->quit();
                threads.clear();
            });

            isFirst = false;
        }

        // qDebug() << "[ThreadPool] Move for" << worker;
        // qDebug() << "[ThreadPool] Move to thread" << thread << "for" << worker << threads.length();
        worker->moveToThread(thread);

        if (!thread->isRunning()) {
            // qDebug() << "[ThreadPool] Start" << thread;
            threads << thread;
            thread->start();
        } else {
            // qDebug() << "[ThreadPool] Ignore start" << thread;
        }

        return thread;
    }

private:
    static bool isFirst;
    static QList<QThread *> threads;
};

#endif // THREAD_POOL_H
