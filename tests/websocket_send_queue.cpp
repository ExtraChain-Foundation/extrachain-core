#include "network/websocket_service.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QWebSocketServer>
#include <QtTest>
#include <sodium.h>

namespace {
class Harness : public WebSocketService {
public:
    explicit Harness(QWebSocket *socket = nullptr) : WebSocketService(socket, nullptr) {
        activated_ = true;
    }
    bool is_active() const override { return !closed_; }
    void wait_for_buffer() { waiting_buffer_space_ = true; }
};

bool replace_wire_send(Harness &service, QList<QByteArray> &sent) {
    const bool disconnected = QObject::disconnect(&service, &WebSocketService::sendMessageInternal,
                                                  &service, nullptr);
    const auto connected = QObject::connect(&service, &WebSocketService::sendMessageInternal, &service,
                     [&sent](const QByteArray &data) { sent.append(data); }, Qt::QueuedConnection);
    return disconnected && connected;
}
}

class WebSocketSendQueueTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(sodium_init() >= 0);
    }

    void cleanupTestCase() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void burstPostsOneDequeueWake() {
        Harness service;
        QSignalSpy wakes(&service, &WebSocketService::needToTryDequeue);
        for (int i = 0; i < 1000; ++i) {
            service.send_message("payload", SocketService::Priority::Low);
        }
        QCOMPARE(service.queue_size(), 1000L);
        QCOMPARE(wakes.count(), 1);
    }

    void dequeuesCannotRunAheadOfQueuedWrites() {
        QWebSocketServer server("fixture", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QWebSocket peer;
        peer.open(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())));
        QTRY_VERIFY(server.hasPendingConnections());
        Harness service(server.nextPendingConnection());
        QList<QByteArray> sent;
        QVERIFY(replace_wire_send(service, sent));
        for (int i = 0; i < 10; ++i) {
            service.send_message(QByteArray::number(i), SocketService::Priority::Low);
        }
        QCoreApplication::sendPostedEvents(&service, QEvent::MetaCall);
        QCOMPARE(service.queue_size(), 9L);
        QVERIFY(sent.empty());
        QTRY_COMPARE(sent.size(), 10);
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(sent[i], QByteArray::number(i));
        }
    }

    void bufferNotificationsCoalesceAndPriorityIsPreserved() {
        QWebSocketServer server("fixture", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QWebSocket peer;
        peer.open(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())));
        QTRY_VERIFY(server.hasPendingConnections());
        Harness service(server.nextPendingConnection());
        QList<QByteArray> sent;
        QVERIFY(replace_wire_send(service, sent));
        QSignalSpy wakes(&service, &WebSocketService::needToTryDequeue);
        service.wait_for_buffer();
        service.send_message("low-one", SocketService::Priority::Low);
        service.send_message("normal", SocketService::Priority::Normal);
        service.send_message("high", SocketService::Priority::High);
        service.send_message("low-two", SocketService::Priority::Low);
        QCOMPARE(wakes.count(), 0);
        for (int i = 0; i < 64; ++i) {
            QVERIFY(QMetaObject::invokeMethod(service.socket(), "bytesWritten", Qt::DirectConnection,
                                              Q_ARG(qint64, 1)));
        }
        QCOMPARE(wakes.count(), 1);
        QTRY_COMPARE(sent.size(), 4);
        QCOMPARE(sent, (QList<QByteArray> { "high", "normal", "low-one", "low-two" }));
    }

    void blockedTransportDoesNotSpinAndCloseDiscardsQueuedWork() {
        Harness service;
        QSignalSpy wakes(&service, &WebSocketService::needToTryDequeue);
        QSignalSpy sends(&service, &WebSocketService::sendMessageInternal);
        service.send_message("first");
        QCoreApplication::sendPostedEvents(&service, QEvent::MetaCall);
        QCOMPARE(wakes.count(), 1);
        for (int i = 0; i < 100; ++i) {
            service.send_message("blocked");
        }
        QCoreApplication::sendPostedEvents(&service, QEvent::MetaCall);
        QCOMPARE(wakes.count(), 1);
        QCOMPARE(service.queue_size(), 101L);
        service.closeSocket();
        service.send_message("after-close");
        QCoreApplication::sendPostedEvents(&service, QEvent::MetaCall);
        QCOMPARE(service.queue_size(), 0L);
        QCOMPARE(sends.count(), 0);
    }

    void highPriorityArrivalAfterWritePreemptsRemainingLowMessages() {
        QWebSocketServer server("fixture", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QWebSocket peer;
        peer.open(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())));
        QTRY_VERIFY(server.hasPendingConnections());
        Harness service(server.nextPendingConnection());
        QList<QByteArray> sent;
        QVERIFY(replace_wire_send(service, sent));
        connect(&service, &WebSocketService::sendMessageInternal, &service,
                [&service](const QByteArray &data) {
            if (data == "low-one") {
                service.send_message("urgent", SocketService::Priority::High);
            }
        }, Qt::QueuedConnection);
        for (const auto *data : { "low-one", "low-two", "low-three" }) {
            service.send_message(data, SocketService::Priority::Low);
        }
        QTRY_COMPARE(sent.size(), 4);
        QCOMPARE(sent, (QList<QByteArray> { "low-one", "urgent", "low-two", "low-three" }));
    }
};

QTEST_GUILESS_MAIN(WebSocketSendQueueTest)
#include "websocket_send_queue.moc"
