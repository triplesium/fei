#include "rendering/dynamic_uniform_buffer.hpp"

#include "test_graphics_device.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstring>
#include <span>

using namespace fei;
using namespace fei::rendering_test;

namespace {

struct TestUniform {
    uint32 first;
    uint32 second;
};

} // namespace

TEST_CASE(
    "DynamicUniformBuffer aligns entries and zeroes padding",
    "[rendering][buffer][uniform]"
) {
    FakeGraphicsDevice device;
    device.uniform_buffer_alignment = 16;
    DynamicUniformBuffer<TestUniform> uniforms;

    uniforms.initialize(device);
    const auto first_offset =
        uniforms.push_back(TestUniform {.first = 1, .second = 2});
    const auto second_offset =
        uniforms.push_back(TestUniform {.first = 3, .second = 4});

    CHECK(uniforms.initialized());
    CHECK(uniforms.stride() == 16);
    CHECK(uniforms.size() == 2);
    CHECK(first_offset == 0);
    CHECK(second_offset == 16);
    REQUIRE(uniforms.bytes().size() == 32);

    TestUniform first {};
    TestUniform second {};
    std::memcpy(&first, uniforms.bytes().data(), sizeof(first));
    std::memcpy(&second, uniforms.bytes().data() + 16, sizeof(second));
    CHECK(first.first == 1);
    CHECK(first.second == 2);
    CHECK(second.first == 3);
    CHECK(second.second == 4);
    for (std::size_t index = sizeof(TestUniform); index < 16; ++index) {
        CHECK(uniforms.bytes()[index] == std::byte {0});
        CHECK(uniforms.bytes()[16 + index] == std::byte {0});
    }
}

TEST_CASE(
    "DynamicUniformBuffer appends aligned entries in one batch",
    "[rendering][buffer][uniform]"
) {
    FakeGraphicsDevice device;
    device.uniform_buffer_alignment = 16;
    DynamicUniformBuffer<TestUniform> uniforms;
    uniforms.initialize(device);

    const auto first_offset =
        uniforms.push_back(TestUniform {.first = 1, .second = 2});
    const std::array batch {
        TestUniform {.first = 3, .second = 4},
        TestUniform {.first = 5, .second = 6},
        TestUniform {.first = 7, .second = 8},
    };
    const auto batch_offset = uniforms.append(batch);
    const auto empty_offset = uniforms.append(std::span<const TestUniform> {});

    CHECK(first_offset == 0);
    CHECK(batch_offset == 16);
    CHECK(empty_offset == 64);
    CHECK(uniforms.size() == 4);
    REQUIRE(uniforms.bytes().size() == 64);

    for (std::size_t index = 0; index < batch.size(); ++index) {
        TestUniform value {};
        std::memcpy(
            &value,
            uniforms.bytes().data() + batch_offset + index * uniforms.stride(),
            sizeof(value)
        );
        CHECK(value.first == batch[index].first);
        CHECK(value.second == batch[index].second);
    }
}

TEST_CASE(
    "DynamicUniformBuffer grows and exposes a dynamic binding",
    "[rendering][buffer][uniform]"
) {
    FakeGraphicsDevice device;
    device.uniform_buffer_alignment = 16;
    RenderQueue queue;
    DynamicUniformBuffer<TestUniform> uniforms;

    uniforms.initialize(device);
    uniforms.push_back(TestUniform {.first = 1});
    uniforms.push_back(TestUniform {.first = 2});
    uniforms.upload(device, queue);

    REQUIRE(uniforms.buffer());
    CHECK(uniforms.capacity() == 2);
    CHECK(uniforms.buffer()->size() == 32);
    CHECK(uniforms.buffer_revision() == 1);
    CHECK(queue.pending_buffer_writes() == 1);

    const auto binding = uniforms.binding();
    REQUIRE(binding);
    CHECK(binding->buffer() == uniforms.buffer());
    CHECK(binding->offset() == 0);
    CHECK(binding->size() == sizeof(TestUniform));

    const auto first_buffer = uniforms.buffer();
    uniforms.clear();
    uniforms.push_back(TestUniform {.first = 3});
    uniforms.upload(device, queue);
    CHECK(uniforms.buffer() == first_buffer);
    CHECK(uniforms.buffer_revision() == 1);

    uniforms.push_back(TestUniform {.first = 4});
    uniforms.push_back(TestUniform {.first = 5});
    uniforms.upload(device, queue);
    CHECK(uniforms.capacity() == 4);
    CHECK(uniforms.buffer() != first_buffer);
    CHECK(uniforms.buffer_revision() == 2);
}

TEST_CASE(
    "DynamicUniformBuffer skips empty uploads",
    "[rendering][buffer][uniform]"
) {
    FakeGraphicsDevice device;
    RenderQueue queue;
    DynamicUniformBuffer<TestUniform> uniforms;

    uniforms.initialize(device);
    uniforms.upload(device, queue);

    CHECK_FALSE(uniforms.buffer());
    CHECK(uniforms.buffer_revision() == 0);
    CHECK(queue.pending_buffer_writes() == 0);
}
