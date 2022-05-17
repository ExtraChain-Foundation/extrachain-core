#ifndef ISOCKETSERVICE_H
#define ISOCKETSERVICE_H

#include "enc/key_private.h"
#include "enc/key_public.h"
#include "utils/exc_utils.h"

class ExtraChainNode;

class EXTRACHAIN_EXPORT SocketService : public QObject {
    Q_OBJECT

public:
    enum class SendType {
        All,
        None,
        // OnlySubNetwork
    };
    Q_ENUM(SendType)

    explicit SocketService(ExtraChainNode &node, QObject *parent = nullptr);
    const QString &identifier() const;
    virtual QString protocolString() const = 0;
    virtual Network::Protocol protocol() const = 0;
    virtual bool isActive() const = 0;
    virtual quint16 port() const = 0;
    virtual quint16 serverPort() const = 0;
    const QString &ip() const;
    const SendType sendType() const;
    int bytesCompressed() const;
    int bytesOutgoing() const;
    int bytesIncoming() const;

protected slots:
    virtual void closeSocket();

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(Network::SocketServiceError code, const QString &errorData);
    void close();
    void activated();
    void finished(); // if threads

protected:
    bool checkFirstMessage(const QString &message);
    QByteArray generateFirstMessage();
    QByteArray prepareSendMessage(const QByteArray &message);
    QByteArray prepareReceiveMessage(const QByteArray &message);

    ExtraChainNode &node;
    QString m_identifier;
    QString m_ip;
    bool m_activated = false;
    int m_bytesIncoming = 0;
    int m_bytesOutgoing = 0;
    int m_bytesCompressed = 0;
    SendType m_sendType = SendType::All;
    // ActorId subNetwork;

    KeyPrivate priv;
    KeyPublic pub;
};

#endif // WEBSOCKETSERVICE_H
