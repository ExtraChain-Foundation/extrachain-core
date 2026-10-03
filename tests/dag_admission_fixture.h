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

    static void limit_file_sync_response(Dag& dag, std::optional<std::size_t> bytes) {
        dag.file_sync_response_budget_ = bytes;
    }

    static int file_sync_batch(Dag& dag) {
        return dag.file_sync_batch();
    }

    static void fit_file_sync_batch(Dag& dag, std::size_t sections, std::size_t bytes) {
        dag.fit_file_sync_batch(sections, bytes);
    }

    static void time_out_section_sync(Dag& dag, const std::string& source) {
        dag.set_status(DagStatus::Sync);
        dag.sync_status_            = DagSyncStatus::Sections;
        dag.sync_source_identifier_ = source;
        dag.timer_tick();
    }

    // Writes a section the way sync installs it: no admission, no indexing.
    static bool store_raw_section(Dag& dag, const Section& section) {
        return dag.hot_section_store_->put(section.id, Json::serialize(section));
    }

    // A live chain starts at its genesis section; a restart resets the cache of one that does not.
    static void start_chain_at_genesis(Dag& dag) {
        dag.first_saved_section_ = SectionId(0);
    }

    static bool pack_history_dirty(Dag& dag) {
        std::lock_guard lock(dag.pack_sync_mutex_);
        return dag.pack_history_dirty_;
    }
};
