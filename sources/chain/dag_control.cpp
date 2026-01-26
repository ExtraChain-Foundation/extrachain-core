/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "chain/dag.h"
#include "managers/extrachain_node.h"
#include "network/network_manager.h"

std::optional<WriteResult> Dag::write_control(const SectionId &section_id, const std::string &hash) {
    if (section_id % CONTROL_INTERVAL_MOD != 0) {
        return std::nullopt;
    }

    auto section = this->read_section(section_id);
    if (!section.has_value()) {
        section = Section { .id = section_id };
    }

    if (section->control.has_value()) {
        if (section->control == hash) {
            return WriteResult::NoChanges;
        }
    }

    section->control = hash;
    auto res         = this->write_section(section.value());
    if (!res.has_value()) {
        return std::nullopt;
    }

    return WriteResult::Write;
}

std::optional<WriteResult> Dag::remove_control(const SectionId &section_id) {
    auto section = this->read_section(section_id);
    if (!section.has_value()) {
        return std::nullopt;
    }

    if (!section->control.has_value()) {
        return WriteResult::Write;
    }

    if (section_id == SectionId(0)) {
        return WriteResult::NoChanges;
    }

    section->control = std::nullopt;
    auto res         = this->write_section(section.value());
    if (!res.has_value()) {
        return std::nullopt;
    }

    return WriteResult::Write;
}

std::optional<DagControl> Dag::find_last_control(const SectionId from, bool disable_break) {
    int j  = 0;
    int jj = 0;

    if (disable_break) {
        auto section = this->read_section(SectionId(0));
        if (section.has_value()) {
            if (!section->control.has_value()) {
                return std::nullopt;
            }
        }
    }

    for (SectionId i = from < 0 ? current_section_ : from; i >= SectionId(0); i--) {
        if (i < first_saved_section_) {
            eCritical("[Dag] Try to find section < current first");
            break;
        }

        auto section = this->read_section(i);
        if (!section.has_value()) {
            if (i % CONTROL_INTERVAL_MOD == 0) {
                eLog("[Dag] No section: {}", i);
                j = 0;
            }
            continue;
        }

        if (section->control.has_value()) {
            if (section->id % CONTROL_INTERVAL_MOD != 0) {
                eCritical("[Dag] Control for section {}", section->id.to_string());
                continue;
            }

            return DagControl { .section_id = i, .control = section->control.value() };
        }

        j += 1;
        if (!disable_break && (j > 37 || jj > 10)) {
            break;
        }
    }

    return std::nullopt;
}

std::optional<DagControl> Dag::read_control(const SectionId &section_id) {
    auto section = read_section(section_id);
    if (!section.has_value()) {
        return std::nullopt;
    }

    if (!section->control.has_value()) {
        return std::nullopt;
    }

    return DagControl { .section_id = section_id, .control = section->control.value() };
}

std::optional<DagControl> Dag::read_control_prev(const SectionId &section_id) {
    for (SectionId i = section_id; i >= SectionId(0); i--) {
        if (i % CONTROL_INTERVAL_MOD == 0) {
            return read_control(i);
        }
    }

    return std::nullopt;
}

std::optional<DagControl> Dag::read_control_next(const SectionId &section_id) {
    for (SectionId i = section_id; i <= current_section_; i++) {
        if (i % CONTROL_INTERVAL_MOD == 0) {
            return read_control(i);
        }
    }

    return std::nullopt;
}

std::optional<std::string> Dag::generate_hash_for_interval(const SectionId &start, std::string &last_hash) {
    SectionId interval_end = (start == SectionId(0))
                                 ? SectionId(0)
                                 : start + (start == 0 ? CONTROL_INTERVAL_DIFF + 1 : CONTROL_INTERVAL_DIFF);

    if (start != 0 && start % 20 == 0) {
#ifdef IS_APP_UI_CLIENT
        eCritical("DAG ERROR 1: {} {}", start, interval_end);
        return std::nullopt;
#endif

        eFatal("DAG ERROR 1: {} {}", start, interval_end);
    }
    if (start != 0 && (start - 1) % 20 != 0) {
#ifdef IS_APP_UI_CLIENT
        eCritical("DAG ERROR 2: {} {}", start, interval_end);
        return std::nullopt;
#endif

        eFatal("DAG ERROR 2: {} {}", start, interval_end);
    }

    auto interval_hash = this->hash_interval(start, interval_end);
    if (!interval_hash.has_value()) {
        return std::nullopt;
    }

    if (start != SectionId(0)) {
        last_hash = Utils::calculate_hash(last_hash + interval_hash.value());
    } else {
        last_hash = interval_hash.value();
    }

    if (last_hash.empty()) {
        return std::nullopt;
    }

    auto res = this->write_control(interval_end, last_hash);
    if (!res.has_value()) {
        return std::nullopt;
    }

    return last_hash;
}

std::optional<std::string> Dag::generate_hash_from_section(const SectionId &start,
                                                           Force            full_generation,
                                                           Force            qt_signals) {
    static bool generating = false;

    if (generating) {
        return std::nullopt;
    }

    std::string last_hash = "";

    if (start > SectionId(0)) {
        auto last_control = this->find_last_control(start - SectionId(1));
        if (last_control.has_value()) {
            last_hash = last_control.value().control;
        } else {
            generating = false;
            return std::nullopt;
        }
    }

    if (start == SectionId(0)) {
        this->generate_hash_for_interval(SectionId(0), last_hash);
        if (full_generation == Force::None) {
            generating = false;
            return last_hash;
        }
    }

    generating = true;

    SectionId current_start = start == SectionId(0) ? SectionId(1) : start;
    for (; current_start <= current_section_ && current_start < current_section_;
         current_start += CONTROL_INTERVAL) {
        if (current_start + CONTROL_INTERVAL > current_section_) {
            break;
        }

        if (current_start > cache_.section()) {
            break;
        }

        this->generate_hash_for_interval(current_start, last_hash);

        if (current_start % 600 == 1) {
            if (!node_enabled.load()) {
                return std::nullopt;
            }

            if (qt_signals == Force::Active) {
                emit node->dagControlProgress(current_start);
            }
        }
    }

    generating = false;
    return last_hash;
}

bool Dag::generate_hash(const SectionId &start_section, Force qt_signals) {
#ifndef IS_APP_CLIENT
    eTemp("[Dag] Generate AcyclicChain controls from {}...", start_section);
#endif

    eTemp("[Dag] Generate hash from {}", start_section);

    if (start_section == BigNumber(0) && current_section_ > 20 && mode_ == DagMode::Light) {
        return false;
    }

    if (qt_signals == Force::Active) {
        emit node->dagControlStarted();
    }

    if (start_section > cache_.section() && start_section != SectionId(0)) {
        emit node->dagControlEnded();
        return true;
    }

    auto result = this->generate_hash_from_section(start_section, Force::Active, qt_signals);

    if (qt_signals == Force::Active) {
        emit node->dagControlEnded();
    }

    return result.has_value();
}

std::optional<std::string> Dag::hash_interval(const SectionId &from, const SectionId &to) {
    std::string section_hashs;

    if (status_ != DagStatus::Sync) {
        eLog("[Dag] Hash interval from {} to {}", from.to_printable_string(), to.to_printable_string());
    }

    if (to > current_section_) {
        eCritical("[Dag] Section to ({}) > current ({})", to.to_printable_string(), current_section_.to_printable_string());
        return std::nullopt;
    }

    for (SectionId i = from; i <= to; i++) {
        auto section = this->read_section(i);

        bool is_empty = false;
        if (!section.has_value()) {
            is_empty = true;
        }
        if (section.has_value() && section->transactions.empty()) {
            is_empty = true;
        }

        if (is_empty) {
            auto hash = Utils::calculate_hash(i.to_string());
            section_hashs += hash;
            continue;
        }

        auto hash = Utils::calculate_hash(i.to_string() + section->calculate_hash());
        section_hashs += hash;
    }

    return Utils::calculate_hash(section_hashs);
}

void Dag::start_control(Force force, Force qt_signals) {
#ifndef IS_APP_CLIENT
    eLog("[Dag] Check controls...");
#endif

    auto find_result = this->find_last_control();
    if (find_result.has_value()) {
        auto section_id = find_result->section_id;

        if (section_id % 20 != 0) {
            eCritical("[Dag] Incorrect control section % 20 != 0: {}, remove wrong control", section_id);
            this->remove_control(section_id);
            this->start_control(Force::Active);
            return;
        }

        if (force == Force::None) {
            return;
        }
    }

    auto find_result_full = this->find_last_control(current_section_, true);

    SectionId start_from = SectionId(0);
    if (find_result_full) {
        if (find_result_full->section_id % 20 == 0) {
            start_from = find_result_full->section_id + 1;
        } else if (find_result) {
            start_from = find_result->section_id;
        }
    }

    this->generate_hash(start_from, qt_signals);
}

void Dag::clear_controls(const SectionId &from) {
    eLog("[Dag] Clear controls from {}...", from);
    for (SectionId i = from; i <= current_section_; i++) {
        auto section = read_section(i);
        if (!section.has_value()) {
            continue;
        }

        if (section->control.has_value()) {
            this->remove_control(i);
        }
    }
}

void Dag::clear_controls_async(const SectionId &from) {
    eLog("[Dag] Clear controls from {}...", from);

    const size_t    num_threads = std::thread::hardware_concurrency();
    const SectionId total       = current_section_ - from + 1;
    const SectionId chunk       = total / num_threads;

    std::vector<std::future<void>> futures;

    for (size_t t = 0; t < num_threads; ++t) {
        SectionId start = from + SectionId(t) * chunk;
        SectionId end   = (t == num_threads - 1) ? current_section_ : start + chunk - 1;

        futures.emplace_back(std::async(std::launch::async, [this, start, end]() {
            for (SectionId i = start; i <= end; i++) {
                auto section = read_section(i);
                if (section.has_value() && section->control.has_value()) {
                    remove_control(i);
                }
            }
        }));
    }

    for (auto &f : futures) {
        f.wait();
    }
}

void Dag::request_control_section(const SectionId &from_top, const Responder &responder) {
    if (search_control_) {
        eTemp("[Dag] No need request control search");
        return;
    }

    SectionId hi = align_down20(from_top < current_section_ ? from_top : current_section_);

    const int       COUNT = 16;
    const SectionId TOTAL = CONTROL_INTERVAL * (COUNT - 1);

    SectionId lo;
    if (hi >= TOTAL) {
        lo = hi - TOTAL;
    } else {
        lo = SectionId(0);
    }

    search_control_ = true;
    emit node->dagSearchControlStarted();
    emit node->dagSyncStart(current_section_, current_section_);

    DagControlRangeRequest req { .from = lo, .to = hi };
    node->network()->send_message(req,
                                  MessageType::DagControlRangeRequest,
                                  responder.empty() ? SendMode::Neighbours : SendMode::Focused,
                                  MessageStatus::Request,
                                  responder.with_new_message_id());
}

void Dag::network_request_control_section(const DagControlRangeRequest &control_request,
                                          const Responder              &responder) {
    if (mode_ == DagMode::Light) {
        return;
    }

    if (!is_aligned20(control_request.from) || !is_aligned20(control_request.to)
        || control_request.to < control_request.from) {
        eLog("[Dag] network_request_control_section Can't send control from {} to {}",
             control_request.to,
             control_request.from);
        return;
    }

    DagControlRangeResponse control_response { .from = control_request.from, .to = control_request.to };
    SectionId               from = SectionId(-1);

    for (SectionId s = control_request.from; s <= control_request.to; s += CONTROL_INTERVAL_MOD) {
        auto dag_control = this->read_control(s);
        if (!dag_control.has_value()) {
            eLog("[Dag] network_request_control_section Can't send control {}, try to regen...", s);

            if (s % 20 == 0) {
                from = s;
            }

            continue;
        }

        control_response.controls.emplace_back(dag_control.value());
    }

    if (from != -1) {
        this->clear_controls(from);
        this->start_control(Force::Active);
        this->network_request_control_section(control_request, responder);
        return;
    }

    responder.send_response(control_response,
                            MessageType::DagControlRangeResponse,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void Dag::network_control_range_response(const DagControlRangeResponse &control_response,
                                         const Responder               &responder) {
    if (!is_aligned20(control_response.from) || !is_aligned20(control_response.to)
        || control_response.to < control_response.from) {
        eLog("[Dag] network_request_control_section Can't read control from {} to {}",
             control_response.to,
             control_response.from);
        search_control_ = false;
        emit node->dagSearchControlEnded();
        return;
    }

    if (responder.luminance() < 2) {
        return;
    }

    SectionId sync_from  = SectionId(-1);
    bool      force_next = false;

    for (int i = 0; i != control_response.controls.size(); i++) {
        auto section_id    = control_response.controls[i].section_id;
        auto control       = control_response.controls[i].control;
        auto local_control = this->read_control(section_id);

        if (!local_control.has_value() && i == 0) {
            force_next = true;
            break;
        }

        if (!local_control.has_value() && i != 0) {
            sync_from = section_id;
            continue;
            break;
        }

        if (local_control.has_value()) {
            if (local_control->control != control) {
                sync_from = section_id;

                if (i == 0) {
                    force_next = true;
                } else {
                    force_next = false;
                }

                break;
            }
        }
    }

    if (sync_from == SectionId(-1) && !force_next) {
        if (sync_last_index_ <= current_section_) {
            search_control_ = false;
            emit node->dagSearchControlEnded();
            this->process_cached_transactions();
            return;
        } else {
            sync_from = current_section_;
        }
    }

    if (force_next) {
        if (control_response.from > 0) {
            const int       COUNT   = 16;
            SectionId       next_hi = (control_response.from >= CONTROL_INTERVAL_MOD)
                                          ? (control_response.from - CONTROL_INTERVAL_MOD)
                                          : SectionId(0);
            const SectionId step    = SectionId(CONTROL_INTERVAL_MOD);
            const SectionId total   = step * (COUNT - 1);
            SectionId       next_lo = (next_hi >= total) ? (next_hi - total) : SectionId(0);

            DagControlRangeRequest req { .from = next_lo, .to = next_hi };
            emit node->dagControlProgress(next_lo);
            node->network()->send_message(req,
                                          MessageType::DagControlRangeRequest,
                                          SendMode::Neighbours,
                                          MessageStatus::Request,
                                          responder.with_new_message_id());
        }

        return;
    }

    if (sync_from != SectionId(-1)) {
        SectionId sync_end = control_response.to;

        eLog("[Dag] Direct request: requesting sections [{}, {}]", sync_from, sync_end);

        auto correct_from = std::max(SectionId(0), sync_from - 50);
        this->remove_sections(correct_from);
        check_status_ = DagSyncStatus::None;
        emit node->dagSyncStart(correct_from, sync_last_index_);
        search_control_ = false;
        emit node->dagSearchControlEnded();
        this->request_file_sections(correct_from,
                                    std::min(sync_from + SYNC_SECTIONS_BATCH, sync_last_index_),
                                    responder.with_new_message_id());
    }
}

void Dag::network_hash_interval(const HashInterval &hash_interval, const Responder &responder) {
    if (status_ != DagStatus::Ready) {
        eLog("[Dag] Hash interval check: ignore", hash_interval);
        return;
    }

    if (responder.luminance() < 2) {
        return;
    }

    auto last_control = this->find_last_control(hash_interval.to - 1);
    if (!last_control.has_value()) {
        eWarning("[Dag] Hash interval check: no last control");
        this->start_control(Force::Active);

        last_control = this->find_last_control(hash_interval.to - 1);
        if (!last_control.has_value()) {
            return;
        }
    }

    if (hash_interval.to > current_section_) {
        eLog("[Dag] Hash interval check: ignore #2");
        return;
    }

    if (hash_interval.to + 100 < current_section_) {
        eLog("[Dag] Hash interval check: ignore #3");
        return;
    }

    if (last_control->section_id != hash_interval.to) {
        eLog("[Dag] Hash interval check: ignore #4");
        return;
    }

    if (last_control->control != hash_interval.hash) {
        eLog("[Dag] Hash interval check: false. Hash interval: {}, last control: {}. Need sync",
             hash_interval,
             last_control);

        return;
        if (current_section_ < hash_interval.to) {
            if (status_ != DagStatus::Ready) {
                status_ = DagStatus::Maybe;
            }

            this->start_check();
        } else {
            this->request_file_sections(hash_interval.from, hash_interval.to, responder);
        }
    } else {
        eLog("[Dag] Hash interval check: true. {}", hash_interval);
    }
}
