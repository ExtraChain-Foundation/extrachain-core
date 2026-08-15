#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "consensus/consensus_engine.h"
#include "consensus/light_client.h"
#include "utils/exc_utils.h"

namespace {
    using namespace ExtraChain::Consensus;
    using Clock = std::chrono::steady_clock;

    struct Fixture {
        Actor<KeyPrivate>              governance;
        std::vector<Actor<KeyPrivate>> actors;
        std::vector<KeyPrivate>        keys;
        ValidatorSet                   validator_set;
        ValidatorSetView               view;

        explicit Fixture(std::size_t validator_count)
            : view(make_view(validator_count)) {
        }

        ValidatorSetView make_view(std::size_t validator_count) {
            governance.create(ActorType::Service);
            for (std::size_t index = 0; index < validator_count; ++index) {
                Actor<KeyPrivate> actor;
                actor.create(ActorType::Service);
                KeyPrivate key;
                key.generate_random();
                auto record =
                    make_validator_record(governance.id(), 1, actor, key, "validator-" + std::to_string(index), 0);
                if (!record.has_value()) {
                    throw std::runtime_error("cannot create a validator record");
                }
                actors.push_back(actor);
                keys.push_back(key);
                validator_set.validators.push_back(record.value());
            }
            auto signed_set = make_validator_set(governance.id(), 1, validator_set.validators, governance);
            if (!signed_set.has_value()) {
                throw std::runtime_error("cannot sign the validator set");
            }
            validator_set = signed_set.value();
            auto created  = ValidatorSetView::create(validator_set);
            if (!created.has_value()) {
                throw std::runtime_error("cannot create the validator view");
            }
            return created.value();
        }
    };

    std::size_t identity_index(const Fixture& fixture, std::string_view validator_id) {
        for (std::size_t index = 0; index < fixture.keys.size(); ++index) {
            if (validator_id_for(fixture.keys[index].public_key()) == validator_id) {
                return index;
            }
        }
        throw std::runtime_error("cannot find the leader identity");
    }

    std::vector<std::pair<std::uint64_t, std::string>> batch_sections(std::uint64_t height) {
        std::vector<std::pair<std::uint64_t, std::string>> sections;
        const auto                                         first = (height - 1) * 20 + 1;
        for (std::uint64_t section = first; section <= height * 20; ++section) {
            sections.emplace_back(section,
                                  "section-data-" + std::to_string(height) + '-' + std::to_string(section));
        }
        return sections;
    }

    SectionBatchManifest batch_manifest(std::uint64_t height) {
        const auto    transaction_hash = "transaction-" + std::to_string(height);
        const auto    sections         = batch_sections(height);
        std::uint64_t payload_bytes    = 0;
        for (const auto& [_, bytes] : sections) {
            payload_bytes += bytes.size();
        }
        return SectionBatchManifest {
            .first_section      = (height - 1) * 20 + 1,
            .last_section       = height * 20,
            .transaction_hashes = { transaction_hash },
            .transaction_root   = calculate_transaction_root({ transaction_hash }),
            .data_root          = calculate_data_root(sections),
            .previous_section_root =
                height == 1 ? "activation-root" : "section-root-" + std::to_string(height - 1),
            .payload_bytes = payload_bytes,
        };
    }

    SectionBatchData batch_data(std::uint64_t height, std::string header_hash) {
        return SectionBatchData {
            .header_hash = std::move(header_hash),
            .manifest    = batch_manifest(height),
            .sections    = batch_sections(height),
        };
    }

    StateCommitmentV2 state_commitment(ConsensusEngine& engine, std::uint64_t height, std::string section_root) {
        std::string previous;
        const auto& parent = engine.safety_state().highest_certificate;
        if (parent.has_value() && parent.value().phase != Phase::Genesis) {
            previous = engine.proposal_for(parent.value().header_hash).value().header.state_commitment;
        }
        return StateCommitmentV2 {
            .network_id                = engine.validators().document().network_id,
            .epoch                     = engine.validators().document().epoch,
            .height                    = height,
            .previous_state_commitment = std::move(previous),
            .section_root              = std::move(section_root),
            .account_state_root        = "account-root-" + std::to_string(height),
            .contract_state_root       = "contract-root-" + std::to_string(height),
            .token_registry_root       = "token-root-" + std::to_string(height),
            .validator_set_hash        = engine.validators().hash(),
        };
    }

    template <typename T>
    T require(std::expected<T, ConsensusError> result, std::string_view step) {
        if (!result.has_value()) {
            throw std::runtime_error(std::string(step) + ": "
                                     + std::to_string(std::to_underlying(result.error())));
        }
        return std::move(result.value());
    }

    void require(std::expected<void, ConsensusError> result, std::string_view step) {
        if (!result.has_value()) {
            throw std::runtime_error(std::string(step) + ": "
                                     + std::to_string(std::to_underlying(result.error())));
        }
    }

    struct BenchmarkOptions {
        std::uint64_t heights         = 100;
        std::size_t   validator_count = 4;
    };

    BenchmarkOptions read_options(int argc, char** argv) {
        if (argc < 1 || argc > 3) {
            throw std::invalid_argument("usage: extrachain-consensus-benchmark [height-count] [validator-count]");
        }
        BenchmarkOptions result;
        if (argc == 1) {
            return result;
        }
        const std::string_view input(argv[1]);
        std::uint64_t          value  = 0;
        const auto             parsed = std::from_chars(input.data(), input.data() + input.size(), value);
        if (parsed.ec != std::errc {} || parsed.ptr != input.data() + input.size() || value < 3
            || value > 10'000'000) {
            throw std::invalid_argument("height-count must be an integer from 3 to 10000000");
        }
        result.heights = value;
        if (argc == 3) {
            const std::string_view count_input(argv[2]);
            std::size_t            count = 0;
            const auto             count_parsed =
                std::from_chars(count_input.data(), count_input.data() + count_input.size(), count);
            if (count_parsed.ec != std::errc {} || count_parsed.ptr != count_input.data() + count_input.size()
                || (count != 4 && count != ShadowCommitteeSize)) {
                throw std::invalid_argument("validator-count must be 4 or 7");
            }
            result.validator_count = count;
        }
        return result;
    }
} // namespace

int main(int argc, char** argv) {
    using namespace ExtraChain::Consensus;
    std::filesystem::path root;
    try {
        const auto options = read_options(argc, argv);
        const auto heights = options.heights;
        root = std::filesystem::temp_directory_path() / ("shadow-bench-" + Utils::generate_random_hex(8));
        std::filesystem::create_directories(root);

        Fixture fixture(options.validator_count);
        auto    light_client = require(LightClientVerifier::create(fixture.validator_set), "create light client");
        std::vector<std::unique_ptr<ConsensusEngine>> engines;
        for (std::size_t index = 0; index < fixture.keys.size(); ++index) {
            auto engine =
                std::make_unique<ConsensusEngine>(fixture.view,
                                                  ValidatorIdentity { .validator_id = validator_id_for(
                                                                          fixture.keys[index].public_key()),
                                                                      .key = fixture.keys[index] },
                                                  std::make_unique<SafetyStore>(
                                                      root / ("validator-" + std::to_string(index) + ".sqlite")));
            require(engine->initialize(), "initialize");
            engines.push_back(std::move(engine));
        }

        std::chrono::nanoseconds proposal_time {};
        std::chrono::nanoseconds stage_time {};
        std::chrono::nanoseconds vote_time {};
        std::chrono::nanoseconds aggregate_time {};
        std::chrono::nanoseconds certificate_time {};
        const auto               start = Clock::now();
        for (std::uint64_t height = 1; height <= heights; ++height) {
            const auto& leader_record = fixture.view.leader(height, 0);
            const auto  leader_index  = identity_index(fixture, leader_record.validator_id);

            auto before = Clock::now();
            auto proposal =
                require(engines[leader_index]->make_proposal(batch_manifest(height),
                                                             state_commitment(*engines[leader_index],
                                                                              height,
                                                                              "section-root-"
                                                                                  + std::to_string(height)),
                                                             0),
                        "make proposal");
            proposal_time += Clock::now() - before;
            const auto proposal_hash = hash_header(proposal.header);

            std::optional<QuorumCertificate> certificate;
            for (std::size_t index = 0; index < engines.size(); ++index) {
                auto& engine = engines[index];
                if (index != leader_index) {
                    require(engine->observe_proposal(proposal), "observe proposal");
                }
                before = Clock::now();
                require(engine->stage_batch(batch_data(height, proposal_hash)), "stage batch");
                stage_time += Clock::now() - before;
                before    = Clock::now();
                auto vote = require(engine->accept_proposal(proposal), "accept proposal");
                vote_time += Clock::now() - before;
                before        = Clock::now();
                auto accepted = require(engines[leader_index]->accept_vote(vote), "accept vote");
                aggregate_time += Clock::now() - before;
                if (accepted.certificate.has_value()) {
                    certificate = std::move(accepted.certificate);
                }
            }
            if (!certificate.has_value()) {
                throw std::runtime_error("cannot form a certificate at height " + std::to_string(height));
            }
            before = Clock::now();
            for (auto& engine : engines) {
                require(engine->accept_certificate(certificate.value()), "accept certificate");
            }
            certificate_time += Clock::now() - before;
            if (height >= 3) {
                const auto light_proofs =
                    require(engines.front()->finality_proofs_after(height - 3, 1), "load light-client proof");
                if (light_proofs.size() != 1) {
                    throw std::runtime_error("light client received a non-contiguous proof stream");
                }
                require(light_client.advance(light_proofs.front()), "advance light client");
            }
        }

        const auto elapsed      = Clock::now() - start;
        const auto milliseconds = [](auto value) {
            return std::chrono::duration<double, std::milli>(value).count();
        };
        const auto seconds      = std::chrono::duration<double>(elapsed).count();
        const auto proofs       = require(engines.front()->finality_proofs_after(0, heights), "load proofs");
        const bool proofs_valid = std::ranges::all_of(proofs, [&](const auto& proof) {
            return engines.front()->verify_finality_proof(proof);
        });
        const bool valid        = engines.front()->safety_state().finalized_height == heights - 2
                           && proofs.size() == heights - 2 && proofs_valid
                           && light_client.trusted_height() == heights - 2;

        std::printf(
            "validators=%zu heights=%llu elapsed_ms=%.3f heights_s=%.3f finalized=%llu proofs=%zu "
            "mobile_light=%llu valid=%s\n",
            options.validator_count,
            static_cast<unsigned long long>(heights),
            milliseconds(elapsed),
            heights / seconds,
            static_cast<unsigned long long>(engines.front()->safety_state().finalized_height),
            proofs.size(),
            static_cast<unsigned long long>(light_client.trusted_height()),
            valid ? "yes" : "no");
        std::printf("proposal_ms=%.3f stage_ms=%.3f vote_ms=%.3f aggregate_ms=%.3f certificate_ms=%.3f\n",
                    milliseconds(proposal_time),
                    milliseconds(stage_time),
                    milliseconds(vote_time),
                    milliseconds(aggregate_time),
                    milliseconds(certificate_time));
        engines.clear();
        std::filesystem::remove_all(root);
        return valid ? 0 : 1;
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "FAIL: %s\n", exception.what());
        if (!root.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
        return 1;
    }
}
