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

#pragma once

#include <QObject>

#include "adapters/qt/qt_compat_global.h"

#include "dfs/dfs_service.h"

/**
 * Qt compatibility facade for ExDFS events.
 *
 * DfsService owns all ExDFS state and work. This facade keeps the signal API
 * used by current Qt clients. New Core code must subscribe to DfsService
 * events instead of these signals.
 */
class EXTRACHAIN_QT_EXPORT DfsController : public QObject, public DfsService {
    Q_OBJECT

public:
    explicit DfsController(ExtraChain::Core::ExtraChainNode* node, QObject* parent = nullptr);
    ~DfsController() override = default;

signals:
    void stored(ActorId owner_id, Dfs::DirRow dir_row);
    void added(ActorId owner_id, Dfs::DirRow dir_row);
    void updated(ActorId owner_id, Dfs::DirRow dir_row);
    void removed(ActorId owner_id, std::string file_id);
    void localRemoved(ActorId owner_id, std::string file_id);

    void uploaded(ActorId owner_id, Dfs::DirRow dir_row);
    void uploadProgress(ActorId owner_id, std::string file_id, int progress);
    void downloaded(ActorId owner_id, Dfs::DirRow dir_row);
    void downloadProgress(ActorId owner_id, std::string file_id, int progress);
    void waitDownloaded(ActorId owner_id, Dfs::DirRow dir_row);

    void collectionDownloaded();
    void collectionChanged(ActorId owner_id, Dfs::DirRow dir_row, HistoricalCollectionRow historical_row);
    void vectorRowAdded(ActorId owner_id, Dfs::DirRow dir_row, DbRow row);
    void vectorRowRemoved(ActorId owner_id, Dfs::DirRow dir_row, DbRow row);

private:
    std::vector<boost::signals2::scoped_connection> qt_event_connections_;
};
