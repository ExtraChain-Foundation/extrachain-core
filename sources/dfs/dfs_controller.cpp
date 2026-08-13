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
 */

#include "dfs/dfs_controller.h"

#include "managers/extrachain_node.h"

DfsController::DfsController(ExtraChain::Core::ExtraChainNode *node, QObject *parent)
    : QObject(parent)
    , DfsService(node) {
    qt_event_connections_.emplace_back(
        stored_event().subscribe([this](const ActorId &owner_id, const Dfs::DirRow &row) {
            emit stored(owner_id, row);
        }));
    qt_event_connections_.emplace_back(
        added_event().subscribe([this](const ActorId &owner_id, const Dfs::DirRow &row) {
            emit added(owner_id, row);
        }));
    qt_event_connections_.emplace_back(
        updated_event().subscribe([this](const ActorId &owner_id, const Dfs::DirRow &row) {
            emit updated(owner_id, row);
        }));
    qt_event_connections_.emplace_back(
        removed_event().subscribe([this](const ActorId &owner_id, const std::string &file_id) {
            emit removed(owner_id, file_id);
        }));
    qt_event_connections_.emplace_back(
        local_removed_event().subscribe([this](const ActorId &owner_id, const std::string &file_id) {
            emit localRemoved(owner_id, file_id);
        }));
    qt_event_connections_.emplace_back(
        uploaded_event().subscribe([this](const ActorId &owner_id, const Dfs::DirRow &row) {
            emit uploaded(owner_id, row);
        }));
    qt_event_connections_.emplace_back(upload_progress_event().subscribe(
        [this](const ActorId &owner_id, const std::string &file_id, int progress) {
            emit uploadProgress(owner_id, file_id, progress);
        }));
    qt_event_connections_.emplace_back(
        downloaded_event().subscribe([this](const ActorId &owner_id, const Dfs::DirRow &row) {
            emit downloaded(owner_id, row);
        }));
    qt_event_connections_.emplace_back(download_progress_event().subscribe(
        [this](const ActorId &owner_id, const std::string &file_id, int progress) {
            emit downloadProgress(owner_id, file_id, progress);
        }));
    qt_event_connections_.emplace_back(
        wait_downloaded_event().subscribe([this](const ActorId &owner_id, const Dfs::DirRow &row) {
            emit waitDownloaded(owner_id, row);
        }));
    qt_event_connections_.emplace_back(collection_downloaded_event().subscribe([this] {
        emit collectionDownloaded();
    }));
    qt_event_connections_.emplace_back(collection_changed_event().subscribe(
        [this](const ActorId &owner_id, const Dfs::DirRow &row, const HistoricalCollectionRow &history) {
            emit collectionChanged(owner_id, row, history);
        }));
    qt_event_connections_.emplace_back(vector_row_added_event().subscribe(
        [this](const ActorId &owner_id, const Dfs::DirRow &row, const DbRow &data) {
            emit vectorRowAdded(owner_id, row, data);
        }));
    qt_event_connections_.emplace_back(vector_row_removed_event().subscribe(
        [this](const ActorId &owner_id, const Dfs::DirRow &row, const DbRow &data) {
            emit vectorRowRemoved(owner_id, row, data);
        }));
}
