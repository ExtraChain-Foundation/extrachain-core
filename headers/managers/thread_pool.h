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

#include <QThread>
#include <QObject>
#include <QCoreApplication>
#include <QDebug>
#include <QMutex>

class ThreadPool : public QObject
{
    Q_OBJECT
private:
    ThreadPool(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~ThreadPool()
    {
    }

public:
    static QThread *addThread(QObject *worker);
    static QThread *addThread(QList<QObject *> workers);
};

#endif // THREAD_POOL_H
