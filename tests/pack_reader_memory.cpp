#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <new>

#include "chain/pack.h"
#include "test_support.h"
#include "utils/exc_utils.h"

// The pack registry keeps up to 16 readers open, and a pack is 50-70 MB. A reader that
// holds its whole file kept up to a gigabyte resident on every node.
namespace {
    std::atomic<std::int64_t> live_bytes { 0 };
    constexpr std::size_t     Prefix = alignof(std::max_align_t);

    void* tracked_new(std::size_t size) {
        auto* block = static_cast<unsigned char*>(std::malloc(size + Prefix));
        if (block == nullptr)
            throw std::bad_alloc();
        *reinterpret_cast<std::size_t*>(block) = size;
        live_bytes.fetch_add(static_cast<std::int64_t>(size), std::memory_order_relaxed);
        return block + Prefix;
    }

    void tracked_delete(void* pointer) noexcept {
        if (pointer == nullptr)
            return;
        auto* block = static_cast<unsigned char*>(pointer) - Prefix;
        live_bytes.fetch_sub(static_cast<std::int64_t>(*reinterpret_cast<std::size_t*>(block)),
                             std::memory_order_relaxed);
        std::free(block);
    }
} // namespace

void* operator new(std::size_t size) {
    return tracked_new(size);
}
void* operator new[](std::size_t size) {
    return tracked_new(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return tracked_new(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return tracked_new(size);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* pointer) noexcept {
    tracked_delete(pointer);
}
void operator delete[](void* pointer) noexcept {
    tracked_delete(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
    tracked_delete(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    tracked_delete(pointer);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    tracked_delete(pointer);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    tracked_delete(pointer);
}

int main() {
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-pack-reader-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    const auto path = directory / "3.pack";

    // Random hex compresses to about 60%, so the file stays around ten megabytes.
    std::map<SectionId, std::string> sections;
    for (long long id = 30000; id < 30000 + static_cast<long long>(Pack::SECTIONS_PER_PACK); ++id)
        sections.emplace(SectionId(id), Utils::generate_random_hex(1600));
    TEST_REQUIRE(Pack::write(path, 3, sections).has_value());
    const auto file_size = std::filesystem::file_size(path);
    TEST_REQUIRE(file_size > 6 * 1024 * 1024);

    {
        const auto before = live_bytes.load();
        auto       reader = Pack::Reader::open(path);
        TEST_REQUIRE(reader.has_value());
        const auto retained = live_bytes.load() - before;
        std::printf("pack of %llu bytes keeps %lld bytes per open reader\n",
                    static_cast<unsigned long long>(file_size),
                    static_cast<long long>(retained));
        std::fflush(stdout);
        TEST_REQUIRE(retained < 2 * 1024 * 1024);

        // Frames come from the file on demand and still decode to the stored sections.
        const auto rows = reader.value().read_range(SectionId(30000), SectionId(39999));
        TEST_REQUIRE_EQ(rows.size(), sections.size());
        for (const auto& [id, payload] : rows)
            TEST_REQUIRE(payload == sections.at(id));
        TEST_REQUIRE(reader.value().read(SectionId(34567)).value() == sections.at(SectionId(34567)));
    }

    // The checksum still covers the whole file.
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(static_cast<std::streamoff>(file_size / 2));
        char byte = 0;
        file.read(&byte, 1);
        file.seekp(static_cast<std::streamoff>(file_size / 2));
        byte = static_cast<char>(byte ^ 0x5a);
        file.write(&byte, 1);
    }
    TEST_REQUIRE(!Pack::Reader::open(path).has_value());

    std::filesystem::remove_all(directory);
}
