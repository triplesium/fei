#include "ecs/schedule.hpp"

#include "ecs/commands.hpp"
#include "ecs/system.hpp"
#include "ecs/system_config.hpp"
#include "ecs/world.hpp"

#include <print>
using namespace fei;

#define TEST_SYSTEM(name)    \
    void name() {            \
        std::println(#name); \
    }

TEST_SYSTEM(test1)
TEST_SYSTEM(test2_before1_after3)
TEST_SYSTEM(test3)
TEST_SYSTEM(test4_after2)
TEST_SYSTEM(test5_in_set1)
TEST_SYSTEM(test6_in_set1_before5)
TEST_SYSTEM(test7_in_set2)
TEST_SYSTEM(test8_in_set2_before4)
TEST_SYSTEM(test9_in_set3)
TEST_SYSTEM(test10_in_set4)
TEST_SYSTEM(chain1_before1)
TEST_SYSTEM(chain2_before1)
TEST_SYSTEM(chain3_before1)
TEST_SYSTEM(nested1)
TEST_SYSTEM(nested2)

struct Set1 : SystemSet<Set1> {};
struct Set2 : SystemSet<Set2> {};
struct Set3 : SystemSet<Set3> {};
struct Set4 : SystemSet<Set4> {};

/*
test3
test6_in_set1_before5
chain1_before1
test10_in_set4
test2_before1_after3
test5_in_set1
chain2_before1
test7_in_set2
test8_in_set2_before4
nested1
nested2
chain3_before1
test4_after2
test9_in_set3
test1
*/

int main() {
    World world;
    world.add_resource(CommandsQueue {});
    Schedule schedule;
    schedule.configure_sets(
        chain(all(Set1 {}), Set2 {}),
        chain(Set4 {}, Set3 {})
    );

    auto test1_config = SystemConfig(test1);
    auto test2_config = SystemConfig(test2_before1_after3);
    auto test3_config = SystemConfig(test3);
    auto test4_config = SystemConfig(test4_after2);
    auto test5_config = SystemConfig(test5_in_set1);
    auto test6_config = SystemConfig(test6_in_set1_before5);
    auto test8_config = SystemConfig(test8_in_set2_before4);
    test2_config.before(test1_config).after(test3_config);
    test4_config.after(test2_config);
    test6_config.before(test5_config);
    test8_config.before(test4_config);
    auto chained_configs =
        chain(chain1_before1, chain(chain2_before1, chain3_before1)) |
        before(test1_config);

    schedule.add_systems(
        std::move(test1_config),
        std::move(test2_config),
        std::move(test3_config),
        std::move(test4_config),
        std::move(test5_config) | in_set<Set1>(),
        std::move(test6_config) | in_set<Set1>(),
        test7_in_set2 | in_set<Set2>(),
        std::move(test8_config) | in_set<Set2>(),
        std::move(chained_configs),
        test9_in_set3 | in_set<Set3>(),
        test10_in_set4 | in_set<Set4>(),
        all(nested1 | in_set<Set4>(), all(nested2)) | in_set<Set2>()
    );

    schedule.sort_systems();
    schedule.run_systems(world);

    return 0;
}
