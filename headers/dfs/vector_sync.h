#pragma once

#include "dfs/dfs_utils.h"
#include "dfs/vector_snapshot.h"
#include "network/responder.h"

namespace ExtraChain::Core {
    class ExtraChainNode;
}

namespace Dfs {
    struct VectorSyncRequest {
        FileLink    link;
        std::string snapshot;
        std::string prefix;
        bool        release = false;
    };
    BOOST_DESCRIBE_STRUCT(VectorSyncRequest, (), (link, snapshot, prefix, release))

    struct VectorSyncReply {
        FileLink                                        link;
        std::string                                     snapshot;
        VectorIndexRoot                                 root;
        std::optional<Packets::DfsVectorContentPackage> metadata;
        std::optional<VectorIndexSlice>                 slice;
    };
    BOOST_DESCRIBE_STRUCT(VectorSyncReply, (), (link, snapshot, root, metadata, slice))

    class VectorSync {
    public:
        explicit VectorSync(ExtraChain::Core::ExtraChainNode* node);
        ~VectorSync();
        void stop();
        void request(const FileLink& link, const std::string& preferred_peer = { });
        bool receive_request(std::string_view data, const Responder& responder);
        bool receive_reply(std::string_view data, const Responder& responder);

    private:
        struct State;
        std::shared_ptr<State> state_;
    };
} // namespace Dfs
