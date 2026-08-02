#include "ecs/world.hpp"
#include "editor/activity.hpp"
#include "editor/component_registry.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "serialization/node.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

using namespace fei;
using namespace fei::editor;
using namespace fei::serialization;

namespace {

struct TestComponent {
    int value {7};
};

struct EncodedValue {};

void register_test_component() {
    Registry::instance().register_cls<TestComponent>().add_property(
        "value",
        &TestComponent::value
    );
}

} // namespace

TEST_CASE(
    "Editor component registry performs generic component mutations",
    "[editor][components]"
) {
    register_test_component();
    ComponentRegistry components;
    REQUIRE(components.register_component<TestComponent>("Test Component"));
    REQUIRE_FALSE(
        components.register_component<TestComponent>("Test Component")
    );

    World world;
    const auto entity = world.entity();

    REQUIRE(components.add_default(world, entity, type_id<TestComponent>()));
    REQUIRE(world.has_component<TestComponent>(entity));
    REQUIRE(world.get_component<TestComponent>(entity).value == 7);

    auto snapshot =
        components.serialize(world, entity, type_id<TestComponent>());
    REQUIRE(snapshot);
    REQUIRE(snapshot->is_object());

    auto set_result = components.set(
        world,
        entity,
        type_id<TestComponent>(),
        SerializedNode::object({
            SerializedField {
                .name = "value",
                .value = SerializedNode::signed_integer(42),
            },
        })
    );
    REQUIRE(set_result);
    REQUIRE(world.get_component<TestComponent>(entity).value == 42);

    REQUIRE(components.remove(world, entity, type_id<TestComponent>()));
    REQUIRE_FALSE(world.has_component<TestComponent>(entity));
}

TEST_CASE("Editor activity log remains bounded", "[editor][activity]") {
    ActivityLog activity;
    for (std::size_t index = 0; index < ActivityLog::c_max_entries + 1;
         ++index) {
        activity.record(
            OperationSource::ExternalAgent,
            "SetProperty",
            std::to_string(index)
        );
    }

    REQUIRE(activity.entries().size() == ActivityLog::c_max_entries);
    REQUIRE(activity.entries().front().sequence == 2);
    REQUIRE(activity.entries().back().source == OperationSource::ExternalAgent);
}

TEST_CASE(
    "Editor previews custom encoded values instead of template type names",
    "[editor][components][preview]"
) {
    Registry::instance().register_type<EncodedValue>();
    ComponentRegistry components;
    REQUIRE(components.codecs().register_codec<EncodedValue>(ValueCodec {
        .encode = [](Ref, std::string_view)
            -> Result<SerializedNode, SerializeError> {
            return SerializedNode::object({
                SerializedField {
                    .name = "$asset",
                    .value = SerializedNode::string("images/face.png"),
                },
            });
        },
        .decode = [](const SerializedNode&,
                     std::string_view) -> Result<Val, DeserializeError> {
            return make_val<EncodedValue>();
        },
    }));

    EncodedValue value;
    auto preview = components.preview(Ref(value));
    REQUIRE(preview);
    REQUIRE(*preview == "images/face.png");
}
