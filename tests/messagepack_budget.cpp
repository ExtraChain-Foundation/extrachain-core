#include "utils/serialization.h"
#include "test_support.h"

int main() {
    const auto array = MessagePack::serialize(std::vector<int> { 1, 2 });
    TEST_REQUIRE(MessagePack::has_bounded_structure(array, 3, 2));
    TEST_REQUIRE(!MessagePack::has_bounded_structure(array, 2, 2));
    TEST_REQUIRE(!MessagePack::has_bounded_structure(array, 3, 1));
    const auto map = MessagePack::serialize(std::map<std::string, int> { { "one", 1 }, { "two", 2 } });
    TEST_REQUIRE(MessagePack::has_bounded_structure(map, 5, 2));
    TEST_REQUIRE(!MessagePack::has_bounded_structure(map, 4, 2));
    TEST_REQUIRE(!MessagePack::has_bounded_structure(map, 5, 1));
    const auto aggregate = MessagePack::serialize(std::vector<std::vector<int>>(64, std::vector<int>(64, 1)));
    TEST_REQUIRE(!MessagePack::has_bounded_structure(aggregate, 4096, 64));
    TEST_REQUIRE(MessagePack::has_bounded_structure(aggregate, 4161, 64));
    std::string nested(17, char(0x91));
    nested += char(0xc0);
    TEST_REQUIRE(!MessagePack::has_bounded_structure(nested, 100, 256, 16));
    TEST_REQUIRE(MessagePack::has_bounded_structure(nested, 100, 256, 17));
    const auto scalar = MessagePack::serialize(std::string(1024 * 1024, '\0'));
    TEST_REQUIRE(MessagePack::has_bounded_structure(scalar, 1, 0, 0));
    TEST_REQUIRE(!MessagePack::has_bounded_structure(scalar, 0, 0, 0));
    TEST_REQUIRE(!MessagePack::has_bounded_structure(scalar.substr(0, scalar.size() - 1), 10, 256));
    TEST_REQUIRE(!MessagePack::has_bounded_structure(array + array, 100, 256));
    TEST_REQUIRE(!MessagePack::has_bounded_structure(std::string("\xdd\xff\xff\xff\xff", 5), 100, 256));
    TEST_REQUIRE(!MessagePack::has_bounded_structure({ }, 100, 256));
    return 0;
}
