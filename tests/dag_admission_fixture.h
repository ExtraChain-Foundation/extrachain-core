#pragma once

#include "chain/dag.h"

class DagAdmissionTestFixture {
public:
    static void refresh_packs(Dag& dag) {
        dag.pack_registry_->rescan();
    }

    static bool history(Dag& dag, const Transaction& transaction, bool repair) {
        const Section section { .id = transaction.section(), .transactions = { transaction } };
        const std::map<SectionId, std::string> sections { { section.id, Json::serialize(section) } };
        return repair ? dag.validated_repair_candidate(sections).has_value()
                      : dag.validated_sync_candidate(sections).has_value();
    }

    static bool pack(Dag& dag, Pack::PackId id, const Pack::Reader& reader) {
        return dag.validate_received_pack(id, reader);
    }

    static bool installed_pack(Dag& dag, const SectionId& target) {
        if (!dag.mark_pack_history_dirty())
            return false;
        std::lock_guard lock(dag.pack_sync_mutex_);
        dag.sync_last_index_ = target;
        return true;
    }

    static bool pack_history_dirty(Dag& dag) {
        std::lock_guard lock(dag.pack_sync_mutex_);
        return dag.pack_history_dirty_;
    }
};
