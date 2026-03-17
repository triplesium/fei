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
    schedule.add_systems(
        test1,
        test2_before1_after3 | before(test1) | after(test3),
        test3,
        test4_after2 | after(test2_before1_after3),
        test5_in_set1 | in_set<Set1>(),
        test6_in_set1_before5 | in_set<Set1>() | before(test5_in_set1),
        test7_in_set2 | in_set<Set2>(),
        test8_in_set2_before4 | in_set<Set2>() | before(test4_after2),
        chain(chain1_before1, chain(chain2_before1, chain3_before1)) |
            before(test1),
        test9_in_set3 | in_set<Set3>(),
        test10_in_set4 | in_set<Set4>(),
        all(nested1 | in_set<Set4>(), all(nested2)) | in_set<Set2>()
    );

    schedule.sort_systems();
    schedule.run_systems(world);

    return 0;
}
