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

#include "managers/thread_pool.h"

QThread *ThreadPool::addThread(QObject *worker)
{
    return ThreadPool::addThread(QList<QObject *>() << worker);
}

QThread *ThreadPool::addThread(QList<QObject *> workers)
{
    static int threadCount = 0;
    // mutex.tryLock();
    static QList<QThread *> threads;
    static bool isFirst = true;

    QThread *thread = new QThread();
    for (const auto &worker : workers)
    {
        worker->moveToThread(thread);
        QObject::connect(thread, SIGNAL(started()), worker, SLOT(process()));
        QObject::connect(worker, SIGNAL(finished()), thread, SLOT(quit()));
        QObject::connect(thread, SIGNAL(finished()), worker, SLOT(deleteLater()));
    }

    QObject::connect(thread, &QThread::finished, [thread, workers]() {
        // threadCount--;
        //        threads.removeOne(thread); // ERROR!!!
        //        qDebug() << "Remove thread"
        //                 << "from pool with new length" << threadCount;
        thread->deleteLater();
    });

    if (isFirst)
    {
        qDebug() << "Connected with qApp";
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, []() {
            qDebug() << "Remove all threads" << threadCount;

            for (auto &&thread : threads)
                thread->quit();
        });

        isFirst = false;
    }

    //    threads << thread;
    // threadCount++;
    //    qDebug() << "Add thread for" << workers << "to pool with new length" << threadCount;
    // mutex.unlock();
    thread->start();
    return thread;
}
