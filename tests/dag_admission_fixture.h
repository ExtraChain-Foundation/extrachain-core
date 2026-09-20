#pragma once

#include "chain/dag.h"

class DagAdmissionTestFixture {
public:
    static bool history(Dag& dag, const Transaction& transaction, bool repair) {
        const Section section { .id = transaction.section(), .transactions = { transaction } };
        const std::map<SectionId, std::string> sections { { section.id, Json::serialize(section) } };
        return repair ? dag.validated_repair_candidate(sections).has_value()
                      : dag.validated_sync_candidate(sections).has_value();
    }
};
