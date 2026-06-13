#include "scripting/scripting_engine.hpp"

#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using namespace fei;

namespace {

enum class ScriptTestEnum { Idle = 1, Active = 2 };

struct ScriptTestReceiver {
    ScriptTestEnum mode {ScriptTestEnum::Idle};
    int value {0};
    int method_calls {0};

    ScriptTestReceiver() = default;
    explicit ScriptTestReceiver(int value) : value(value) {}

    void set_value(int next) {
        value = next;
        ++method_calls;
    }

    void set_mode(ScriptTestEnum next) {
        mode = next;
        ++method_calls;
    }

    void copy_to(ScriptTestReceiver& target) const {
        target.mode = mode;
        target.value = value;
    }
};

void register_script_test_metadata() {
    auto& registry = Registry::instance();
    registry.register_enum<ScriptTestEnum>()
        .add_enumerator("Idle", static_cast<std::int64_t>(ScriptTestEnum::Idle))
        .add_enumerator(
            "Active",
            static_cast<std::int64_t>(ScriptTestEnum::Active)
        );

    registry.register_cls<ScriptTestReceiver>()
        .add_property("mode", &ScriptTestReceiver::mode)
        .add_property("value", &ScriptTestReceiver::value)
        .add_method("set_value", &ScriptTestReceiver::set_value)
        .add_method("set_mode", &ScriptTestReceiver::set_mode)
        .add_method("copy_to", &ScriptTestReceiver::copy_to)
        .add_constructor<ScriptTestReceiver, int>();
}

ScriptingEngine make_test_engine() {
    register_script_test_metadata();

    ScriptingEngine engine;
    engine.register_type(type<ScriptTestReceiver>());
    engine.register_enum(Registry::instance().get_enum<ScriptTestEnum>());
    return engine;
}

} // namespace

TEST_CASE(
    "ScriptingEngine reads properties, writes properties, and calls methods",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;
    receiver.value = 4;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
        receiver.value = receiver.value + 3
        receiver:set_value(receiver.value + 5)
    )");

    REQUIRE(receiver.value == 12);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE(
    "ScriptingEngine constructs reflected objects from Lua",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
        local created = ScriptTestReceiver.new(21)
        created.mode = ScriptTestEnum.Active
        created:copy_to(receiver)
    )");

    REQUIRE(receiver.value == 21);
    REQUIRE(receiver.mode == ScriptTestEnum::Active);
}

TEST_CASE(
    "ScriptingEngine calls Lua functions with reflected refs",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;
    receiver.value = 10;

    engine.run_script(R"(
        function update_receiver(target)
            target.value = target.value + 8
            target:set_mode(ScriptTestEnum.Active)
        end
    )");

    REQUIRE(engine.call_function(
        "update_receiver",
        std::vector<Ref> {make_ref(receiver)}
    ));
    REQUIRE(receiver.value == 18);
    REQUIRE(receiver.mode == ScriptTestEnum::Active);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE(
    "ScriptingEngine passes enum constants to reflected methods",
    "[scripting][enum]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script("receiver:set_mode(ScriptTestEnum.Active)");

    REQUIRE(receiver.mode == ScriptTestEnum::Active);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE(
    "ScriptingEngine assigns enum constants to reflected properties",
    "[scripting][enum]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script("receiver.mode = ScriptTestEnum.Active");

    REQUIRE(receiver.mode == ScriptTestEnum::Active);
    REQUIRE(receiver.method_calls == 0);
}
