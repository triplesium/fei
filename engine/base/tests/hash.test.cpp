#include "base/hash.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

struct BaseHashPoint {
    int x;
    int y;

    bool operator==(const BaseHashPoint&) const = default;
};

MAKE_STD_HASHABLE(BaseHashPoint, x, y)

TEST_CASE("hash helpers combine scalar and range values", "[base][hash]") {
    std::size_t seed = 0;
    fei::hash_combine(seed, 1);
    fei::hash_combine(seed, std::string {"two"});

    REQUIRE(seed == fei::hash_combine_all(1, std::string {"two"}));
    REQUIRE(fei::hash_combine_all(1, 2) != fei::hash_combine_all(2, 1));

    std::vector<int> first {1, 2, 3};
    std::vector<int> second {1, 2, 3};
    std::vector<int> third {3, 2, 1};

    REQUIRE(fei::hash_combine_all(first) == fei::hash_combine_all(second));
    REQUIRE(fei::hash_combine_all(first) != fei::hash_combine_all(third));

    REQUIRE(std::hash<BaseHashPoint> {}({1, 2}) == fei::hash_combine_all(1, 2));
}
