#ifndef IPCONTROLLER_H
#define IPCONTROLLER_H

#include "datastorage/actor.h"
#include "datastorage/private_profile.h"
#include "utils/autologinhash.h"
#include "utils/dfs_utils.h"

class ExtraChainNode;
class IPLoader;
struct IPConnection {
    IPConnection(const std::string ip, const int port, const std::string actor);
    IPConnection(DFS::Packets::IPConnection ipConnection);
    bool operator==(const IPConnection& ipConnection) const;

    std::string ip;
    std::string actor;
    int port;
    float rank = 0.0;
};

class EXTRACHAIN_EXPORT IPController {
    ExtraChainNode& m_node;
    std::vector<IPConnection> ipConnections;
    IPLoader* ipLoader;

public:
    explicit IPController(ExtraChainNode& node);
    ~IPController();

    void connectToIP(const std::string& ip, const int& port, const std::string& actor);
    void connectToIP(const DFSP::IPConnection& ipConnection);
    void getIpConnections(const ActorId& actor);
    std::vector<IPConnection> allIpConnections();
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
    std::vector<IPConnection> load();
    void update(const IPConnection& ipconnection, const IPConnectionEvent& connectionEvent);
    int getCount(const IPConnection& ipconnection, const IPConnectionEvent& connectionEvent);
    float ranking(IPConnection &ipconnection);
    DBRow makeDBRow(const std::string ip, const uint64_t port, const std::string anchor,
                    const uint64_t connectedCount = 0, const uint64_t disconnectedCount = 0);
    DFSP::IPConnection convertIntoIPConnection(const IPConnection& ipconnection);
};

#endif // IPCONTROLLER_H
