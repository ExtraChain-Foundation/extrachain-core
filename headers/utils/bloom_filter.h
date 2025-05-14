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

#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <cstring>

#include "blockchain/actor_id.h"

class ActorSynchronizer {
private:
    std::vector<ActorId> localActors;

    // Количество корзин (256 = 1 байт для индекса корзины)
    static constexpr size_t BUCKET_COUNT = 256;

    // Хеш-функция для актора (FNV-1a 64-бит)
    static uint64_t hashActor(const std::string& str) {
        const uint64_t FNV_PRIME        = 1099511628211ULL;
        const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

        uint64_t hash = FNV_OFFSET_BASIS;
        for (char c : str) {
            hash ^= static_cast<uint64_t>(c);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    // Получение индекса корзины для актора
    static uint8_t getBucketIndex(const std::string& actorStr) {
        uint64_t hash = hashActor(actorStr);
        return static_cast<uint8_t>(hash % BUCKET_COUNT);
    }

    // Вычисление хеша содержимого корзины
    static uint64_t computeBucketHash(const std::vector<std::string>& bucketItems) {
        // Если корзина пуста
        if (bucketItems.empty()) {
            return 0;
        }

        // Сортируем для стабильного хеширования
        std::vector<std::string> sortedItems = bucketItems;
        std::sort(sortedItems.begin(), sortedItems.end());

        // Удаляем дубликаты
        auto last = std::unique(sortedItems.begin(), sortedItems.end());
        sortedItems.erase(last, sortedItems.end());

        // Объединяем все элементы и хешируем
        std::string combined;
        for (const auto& item : sortedItems) {
            combined += item;
        }

        return hashActor(combined);
    }

    // Структура для хранения хешей корзин
    struct BucketHashes {
        uint64_t hashes[BUCKET_COUNT];

        BucketHashes() {
            memset(hashes, 0, sizeof(hashes));
        }
    };

    // Создание хешей корзин из вектора акторов
    BucketHashes createBucketHashes() const {
        // Распределяем акторы по корзинам
        std::unordered_map<uint8_t, std::vector<std::string>> buckets;

        for (const auto& actor : localActors) {
            std::string actorStr    = actor.to_string();
            uint8_t     bucketIndex = getBucketIndex(actorStr);
            buckets[bucketIndex].push_back(actorStr);
        }

        // Вычисляем хеш для каждой корзины
        BucketHashes result;

        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            auto it = buckets.find(static_cast<uint8_t>(i));
            if (it != buckets.end()) {
                result.hashes[i] = computeBucketHash(it->second);
            }
        }

        return result;
    }

    // Сериализация хешей корзин для отправки
    static std::vector<uint8_t> serializeBucketHashes(const BucketHashes& hashes) {
        std::vector<uint8_t> result(sizeof(hashes.hashes));
        memcpy(result.data(), hashes.hashes, sizeof(hashes.hashes));
        return result;
    }

    // Десериализация хешей корзин
    static BucketHashes deserializeBucketHashes(const std::vector<uint8_t>& data) {
        BucketHashes result;
        memcpy(result.hashes, data.data(), std::min(sizeof(result.hashes), data.size()));
        return result;
    }

    // Найти индексы корзин, которые различаются
    static std::vector<uint8_t> findDifferentBuckets(const BucketHashes& localHashes,
                                                     const BucketHashes& remoteHashes) {
        std::vector<uint8_t> differentBuckets;

        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            if (localHashes.hashes[i] != remoteHashes.hashes[i]) {
                differentBuckets.push_back(static_cast<uint8_t>(i));
            }
        }

        return differentBuckets;
    }

    // Получить акторы из указанных корзин
    std::vector<ActorId> getActorsFromBuckets(const std::vector<uint8_t>& bucketIndices) const {
        std::vector<ActorId> result;

        // Создаем набор индексов для быстрой проверки
        std::unordered_set<uint8_t> indices(bucketIndices.begin(), bucketIndices.end());

        // Множество для отслеживания уже добавленных акторов (устраняем дубликаты)
        std::unordered_set<std::string> addedActors;

        // Проверяем каждый актор
        for (const auto& actor : localActors) {
            std::string actorStr = actor.to_string();

            // Пропускаем, если актор уже был добавлен
            if (addedActors.find(actorStr) != addedActors.end()) {
                continue;
            }

            uint8_t bucketIndex = getBucketIndex(actorStr);

            if (indices.find(bucketIndex) != indices.end()) {
                result.push_back(actor);
                addedActors.insert(actorStr);
            }
        }

        return result;
    }

public:
    ActorSynchronizer() {
    }

    void setActors(const std::vector<ActorId>& actors) {
        localActors = actors;
    }

    // Создание запроса на синхронизацию (для отправки другой стороне)
    std::vector<uint8_t> createSyncRequest() {
        // Создаем хеши корзин для локальных акторов
        auto bucketHashes = createBucketHashes();

        // Сериализуем хеши корзин для отправки
        return serializeBucketHashes(bucketHashes);
    }

    // Обработка полученного запроса (возвращает ID, которые следует отправить)
    std::vector<ActorId> processSyncRequest(const std::vector<uint8_t>& requestData) {
        // Десериализуем полученные хеши корзин
        auto remoteBucketHashes = deserializeBucketHashes(requestData);

        // Создаем хеши локальных корзин
        auto localBucketHashes = createBucketHashes();

        // Находим различающиеся корзины
        auto differentBuckets = findDifferentBuckets(localBucketHashes, remoteBucketHashes);

        // Получаем акторы из различающихся корзин
        return getActorsFromBuckets(differentBuckets);
    }

    // Применение полученных ID (обновление локального множества)
    void applyReceivedIds(const std::vector<ActorId>& receivedIds) {
        // Множество для быстрой проверки уже имеющихся акторов
        std::unordered_set<std::string> existingActors;
        for (const auto& actor : localActors) {
            existingActors.insert(actor.to_string());
        }

        // Добавляем только уникальные акторы
        for (const auto& actor : receivedIds) {
            std::string actorStr = actor.to_string();
            if (existingActors.find(actorStr) == existingActors.end()) {
                localActors.push_back(actor);
                existingActors.insert(actorStr);
            }
        }
    }
};
