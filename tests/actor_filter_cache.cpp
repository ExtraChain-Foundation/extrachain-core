#include "precompiled.h"
#include "chain/actor_filter.h"

#include <QtTest/QtTest>
#include <random>

namespace {
uint64_t legacyHash(const std::string &text) {
    uint64_t hash = 14695981039346656037ULL;
    for (char byte : text) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::vector<uint8_t> legacyRequest(const std::vector<ActorId> &actors) {
    std::array<std::set<std::string>, 256> buckets;
    for (const auto &actor : actors) {
        buckets[legacyHash(actor.to_string()) % buckets.size()].insert(actor.to_string());
    }
    std::array<uint64_t, 256> hashes {};
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        std::string text;
        for (const auto &actor : buckets[i]) {
            text += actor;
        }
        hashes[i] = text.empty() ? 0 : legacyHash(text);
    }
    std::vector<uint8_t> bytes(sizeof(hashes));
    std::memcpy(bytes.data(), hashes.data(), bytes.size());
    return bytes;
}

std::vector<ActorId> legacyDifference(const std::vector<ActorId> &actors,
                                    const std::vector<uint8_t> &remote) {
    const auto local = legacyRequest(actors);
    std::array<uint64_t, 256> remoteHashes {}, localHashes {};
    std::memcpy(remoteHashes.data(), remote.data(), std::min(remote.size(), sizeof(remoteHashes)));
    std::memcpy(localHashes.data(), local.data(), sizeof(localHashes));
    std::vector<ActorId> result;
    for (const auto &actor : std::set<ActorId>(actors.begin(), actors.end())) {
        const auto bucket = legacyHash(actor.to_string()) % localHashes.size();
        if (localHashes[bucket] != remoteHashes[bucket]) {
            result.push_back(actor);
        }
    }
    return result;
}

std::vector<ActorId> sample(std::size_t count) {
    std::mt19937_64 random(42);
    std::vector<ActorId> actors;
    actors.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        actors.emplace_back(fmt::format("{:x}", random()));
    }
    return actors;
}
}

class ActorFilterCacheTest : public QObject {
    Q_OBJECT
private slots:
    void wireHashesAndDifferenceMatchLegacy() {
        auto actors = sample(4096);
        const std::vector<ActorId> duplicates(actors.begin(), actors.begin() + 32);
        actors.insert(actors.end(), duplicates.begin(), duplicates.end());
        std::reverse(actors.begin(), actors.end());
        ActorSynchronizer synchronizer;
        synchronizer.set_actors(actors);
        QCOMPARE(synchronizer.create_sync_request(), legacyRequest(actors));
        auto remoteActors = sample(3000);
        const auto remote = legacyRequest(remoteActors);
        QCOMPARE(synchronizer.process_sync_request(remote), legacyDifference(actors, remote));
        QCOMPARE(synchronizer.process_sync_request(legacyRequest(actors)).size(), std::size_t(0));
    }

    void additionsDuplicatesAndReplacementInvalidatePrecisely() {
        auto actors = sample(1024);
        ActorSynchronizer synchronizer;
        synchronizer.set_actors(actors);
        const auto original = synchronizer.create_sync_request();
        synchronizer.apply_received_ids(actors);
        QCOMPARE(synchronizer.create_sync_request(), original);
        const auto added = ActorId("1234");
        synchronizer.apply_received_ids({ added, added });
        actors.push_back(added);
        QCOMPARE(synchronizer.create_sync_request(), legacyRequest(actors));
        QCOMPARE(synchronizer.process_sync_request(original), legacyDifference(actors, original));
        synchronizer.set_actors({ added });
        QCOMPARE(synchronizer.create_sync_request(), legacyRequest({ added }));
        synchronizer.set_actors({});
        QCOMPARE(synchronizer.create_sync_request(), legacyRequest({}));
    }

    void malformedRequestsRetainLegacyInterpretation() {
        const auto actors = sample(512);
        ActorSynchronizer synchronizer;
        synchronizer.set_actors(actors);
        for (std::size_t size : { 1, 7, 1023, 2047, 2048, 2049 }) {
            const std::vector<uint8_t> remote(size, 17);
            QCOMPARE(synchronizer.process_sync_request(remote), legacyDifference(actors, remote));
        }
    }

    void repeatedLargeIndexAndCopyStayConsistent() {
        const auto actors = sample(35000);
        ActorSynchronizer synchronizer;
        synchronizer.set_actors(actors);
        const auto expected = legacyRequest(actors);
        QCOMPARE(synchronizer.create_sync_request(), expected);
        for (int i = 0; i < 100; ++i) {
            QCOMPARE(synchronizer.create_sync_request(), expected);
            QVERIFY(synchronizer.process_sync_request(expected).empty());
        }
        auto copy = synchronizer;
        copy.apply_received_ids({ ActorId("5678") });
        QCOMPARE(synchronizer.create_sync_request(), expected);
        QVERIFY(copy.create_sync_request() != expected);
    }
};

QTEST_GUILESS_MAIN(ActorFilterCacheTest)
#include "actor_filter_cache.moc"
