#ifndef IPCONTROLLER_H
#define IPCONTROLLER_H

#include "datastorage/actor.h"
#include "datastorage/private_profile.h"
#include "utils/autologinhash.h"
#include "utils/dfs_utils.h"

class ExtraChainNode;

struct IPConnection {
    IPConnection(const std::string ip, const int port);
    bool operator==(const IPConnection& ipConnection) const;

    std::string ip;
    int port;
};

class EXTRACHAIN_EXPORT IPController {
    ExtraChainNode& m_node;
    std::vector<IPConnection> ipConnections;

public:
    explicit IPController(ExtraChainNode& node);

    void connectToIP(const std::string &ip, const int &port);
    void connectToIP(const DFSP::IPConnection& ipConnection);
    void getIpConnections(const ActorId &actor);
};

#endif // IPCONTROLLER_H
