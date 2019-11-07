#include "managers/thread_pool.h"

QThread *ThreadPool::addThread(QObject *worker)
{
    return ThreadPool::addThread(QList<QObject *>() << worker);
}

QThread *ThreadPool::addThread(QList<QObject *> workers)
{
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
        threads.removeAt(threads.indexOf(thread));
        qDebug() << "Remove thread for" << workers << "from pool with length" << threads.length();
        thread->deleteLater();
    });

    if (isFirst)
    {
        qDebug() << "Connected with qApp";
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, []() {
            qDebug() << "Remove all threads" << threads.count();

            for (auto &&thread : threads)
                thread->quit();
        });

        isFirst = false;
    }

    qDebug() << "Add thread for" << workers << "to pool with length" << threads.length();
    thread->start();
    threads << thread;

    return thread;
}
