#pragma once

#include "base/log.hpp"
#include "base/types.hpp"
#include "graphics/buffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "rendering/render_queue.hpp"

#include <bit>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace ets {

template<typename T>
    requires std::is_trivially_copyable_v<T>
class DynamicUniformBuffer {
  public:
    DynamicUniformBuffer() = default;
    DynamicUniformBuffer(const DynamicUniformBuffer&) = delete;
    DynamicUniformBuffer& operator=(const DynamicUniformBuffer&) = delete;
    DynamicUniformBuffer(DynamicUniformBuffer&&) noexcept = default;
    DynamicUniformBuffer& operator=(DynamicUniformBuffer&&) noexcept = default;

    void initialize(const GraphicsDevice& device) {
        const auto alignment = device.uniform_buffer_offset_alignment();
        if (alignment == 0) {
            fatal("Uniform buffer offset alignment cannot be zero");
        }
        if (m_stride != 0) {
            if (m_alignment != alignment) {
                fatal("DynamicUniformBuffer alignment cannot change");
            }
            return;
        }
        if (sizeof(T) >
            std::numeric_limits<std::size_t>::max() - (alignment - 1)) {
            fatal("DynamicUniformBuffer stride overflows");
        }
        m_alignment = alignment;
        m_stride = ((sizeof(T) + alignment - 1) / alignment) * alignment;
    }

    void clear() { m_data.clear(); }

    uint32 append(std::span<const T> values) {
        if (m_stride == 0) {
            fatal("DynamicUniformBuffer must be initialized before use");
        }
        constexpr auto max_offset =
            static_cast<std::size_t>(std::numeric_limits<uint32>::max());
        const auto first_offset = m_data.size();
        if (first_offset > max_offset) {
            fatal("DynamicUniformBuffer exceeds the dynamic offset range");
        }
        if (values.empty()) {
            return static_cast<uint32>(first_offset);
        }
        if (values.size() > (max_offset - first_offset) / m_stride) {
            fatal("DynamicUniformBuffer exceeds the dynamic offset range");
        }

        m_data.resize(first_offset + values.size() * m_stride, std::byte {0});
        for (std::size_t index = 0; index < values.size(); ++index) {
            std::memcpy(
                m_data.data() + first_offset + index * m_stride,
                &values[index],
                sizeof(T)
            );
        }
        return static_cast<uint32>(first_offset);
    }

    uint32 push_back(const T& value) {
        return append(std::span<const T> {&value, 1});
    }

    void reserve(std::size_t count, const GraphicsDevice& device) {
        initialize(device);
        if (count <= m_capacity) {
            return;
        }

        const auto max_capacity = std::numeric_limits<uint32>::max() / m_stride;
        if (count > max_capacity) {
            fatal("DynamicUniformBuffer exceeds the dynamic offset range");
        }

        const auto power_of_two_limit = std::bit_floor(max_capacity);
        const auto new_capacity =
            count > power_of_two_limit ? count : std::bit_ceil(count);
        m_buffer = device.create_buffer(
            BufferDescription {
                .size = new_capacity * m_stride,
                .usages = BufferUsages::Uniform,
            }
        );
        m_capacity = new_capacity;
        ++m_buffer_revision;
    }

    void upload(const GraphicsDevice& device, const RenderQueue& queue) {
        if (m_data.empty()) {
            return;
        }
        reserve(size(), device);
        queue.write_buffer(m_buffer, 0, m_data.data(), m_data.size());
    }

    [[nodiscard]] bool initialized() const { return m_stride != 0; }
    [[nodiscard]] bool empty() const { return m_data.empty(); }
    [[nodiscard]] std::size_t size() const {
        return m_stride == 0 ? 0 : m_data.size() / m_stride;
    }
    [[nodiscard]] std::size_t capacity() const { return m_capacity; }
    [[nodiscard]] std::size_t stride() const { return m_stride; }
    [[nodiscard]] std::span<const std::byte> bytes() const { return m_data; }
    [[nodiscard]] const std::shared_ptr<Buffer>& buffer() const {
        return m_buffer;
    }
    [[nodiscard]] uint64 buffer_revision() const { return m_buffer_revision; }
    [[nodiscard]] std::shared_ptr<BufferRange> binding() const {
        if (!m_buffer) {
            return nullptr;
        }
        return std::make_shared<BufferRange>(m_buffer, 0, sizeof(T));
    }

  private:
    std::vector<std::byte> m_data;
    std::shared_ptr<Buffer> m_buffer;
    std::size_t m_alignment {0};
    std::size_t m_stride {0};
    std::size_t m_capacity {0};
    uint64 m_buffer_revision {0};
};

} // namespace ets
