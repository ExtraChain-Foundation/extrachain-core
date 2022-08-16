#ifndef IPCONTROLLER_H
#define IPCONTROLLER_H

#include "datastorage/actor.h"
#include "datastorage/private_profile.h"
#include "network/isocket_service.h"
#include "utils/autologinhash.h"
#include "utils/dfs_utils.h"

class ExtraChainNode;
class IPLoader;
struct IPConnection {
    struct Rate {
        Rate() = default;
        Rate(const int& rPingTime, const int& rSentData);
        int connectionRate = 1;
        int pingTime;
        int sentData;
        int bandWidth;
        int lastRate;
        int calcRate();
    };

    IPConnection(const std::string ip, const int port, const std::string actor, const std::string identifier);
    IPConnection(const SocketService* socket, const std::string actor);
    IPConnection(DFS::Packets::IPConnection ipConnection);
    bool operator==(const IPConnection& ipConnection) const;

    std::string ip;
    std::string actor;
    int port;
    std::string identifier;
    std::vector<Rate> rates;
};

class EXTRACHAIN_EXPORT IPController {
    ExtraChainNode& m_node;
    std::vector<IPConnection> ipConnections;
    IPLoader* ipLoader;

public:
    explicit IPController(ExtraChainNode& node);
    ~IPController();

    void connectToIp(IPConnection connection);

    void connectToIP(const std::string& ip, const int& port, const std::string& actor,
                     const std::string& identifier);
    void connectToIP(const DFSP::IPConnection& ipConnection);
    void getIpConnections(const ActorId& actor);
    std::vector<IPConnection> allIpConnections();
    void saveRate(const IPConnection& ipconnection, IPConnection::Rate& rate);
};

class IPLoader {
public:
    enum IPConnectionEvent {
        Connected,
        Disconnected
    };

    IPLoader();

    void save(IPConnection ipConnection);
    void save(std::vector<IPConnection> ipConnections);
    void saveRate(const IPConnection& ipconnection, IPConnection::Rate& rate);
    std::vector<IPConnection> load();
    void update(const IPConnection& ipconnection, const IPConnectionEvent& connectionEvent);
    int getCount(const IPConnection& ipconnection, const IPConnectionEvent& connectionEvent);
    DBRow makeDBRow(const IPConnection& ipconnection, const uint64_t connectedCount = 0,
                    const uint64_t disconnectedCount = 0);
    DBRow makeRateDBRow(const IPConnection& ipconnection, const IPConnection::Rate& rate) const;

    DFSP::IPConnection convertIntoIPConnection(const IPConnection& ipconnection);
};

#endif // IPCONTROLLER_H
