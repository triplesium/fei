#include "rendering/gpu_vector.hpp"

#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;
using namespace ets::rendering_test;

TEST_CASE(
    "GpuVector grows geometrically and queues uploads",
    "[rendering][buffer]"
) {
    FakeGraphicsDevice device;
    RenderQueue queue;
    GpuVector<uint32> values {BufferUsages::Vertex};

    values.push_back(10);
    values.push_back(20);
    values.upload(device, queue);

    REQUIRE(values.size() == 2);
    REQUIRE(values.capacity() == 2);
    REQUIRE(values.buffer());
    REQUIRE(values.buffer()->size() == 2 * sizeof(uint32));
    REQUIRE(values.buffer_revision() == 1);
    REQUIRE(queue.pending_buffer_writes() == 1);
    CHECK(values.values()[0] == 10);
    CHECK(values.values()[1] == 20);

    const auto first_buffer = values.buffer();
    values.clear();
    values.push_back(30);
    values.upload(device, queue);

    CHECK(values.buffer() == first_buffer);
    CHECK(values.buffer_revision() == 1);
    CHECK(queue.pending_buffer_writes() == 2);

    values.push_back(40);
    values.push_back(50);
    values.upload(device, queue);

    CHECK(values.capacity() == 4);
    CHECK(values.buffer() != first_buffer);
    CHECK(values.buffer_revision() == 2);
    CHECK(queue.pending_buffer_writes() == 3);
}

TEST_CASE(
    "GpuVector does not allocate for an empty upload",
    "[rendering][buffer]"
) {
    FakeGraphicsDevice device;
    RenderQueue queue;
    GpuVector<uint32> values {BufferUsages::Index};

    values.upload(device, queue);

    CHECK_FALSE(values.buffer());
    CHECK(values.capacity() == 0);
    CHECK(values.buffer_revision() == 0);
    CHECK(queue.pending_buffer_writes() == 0);
}
