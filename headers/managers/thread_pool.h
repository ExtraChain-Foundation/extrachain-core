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
