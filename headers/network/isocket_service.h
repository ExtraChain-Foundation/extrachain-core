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

    explicit SocketService(ExtraChainNode *node, QObject *parent = nullptr);
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
    bool                      isConstant() const;
    void                      setConstant(bool isConstant);

public:
    virtual void sendMessage(const QByteArray &data) = 0;
    virtual void final() = 0;

protected slots:
    virtual void closeSocket();

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(Network::SocketServiceError code, const QString &errorData);
    void close();
    void activated();
    void finished(); // if threads
    void shareConnections(const QJsonArray connectionsArr);

protected:
    bool       checkFirstMessage(const QString &message, const bool canUseConnection);
    QByteArray generateFirstMessage();
    QByteArray prepareSendMessage(const QByteArray &message);
    QByteArray prepareReceiveMessage(const QByteArray &message);

    ExtraChainNode *node;
    QString m_identifier;
    QString m_ip;
    quint16 m_port = 0;
    bool m_activated = false;
    bool            m_needToDelete;
    int m_bytesIncoming = 0;
    int m_bytesOutgoing = 0;
    int m_bytesCompressed = 0;
    SendType m_sendType = SendType::All;
    std::atomic_bool m_isConstant      = false;
    // ActorId subNetwork;

    KeyPrivate priv;
    KeyPublic pub;
};

#endif // WEBSOCKETSERVICE_H
