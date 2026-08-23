#pragma once

#include "base/log.hpp"
#include "base/types.hpp"
#include "graphics/buffer.hpp"
#include "graphics/graphics_device.hpp"
#include "rendering/render_queue.hpp"

#include <bit>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace ets {

template<typename T>
    requires std::is_trivially_copyable_v<T>
class GpuVector {
  public:
    explicit GpuVector(BitFlags<BufferUsages> usages) : m_usages(usages) {}

    GpuVector(const GpuVector&) = delete;
    GpuVector& operator=(const GpuVector&) = delete;
    GpuVector(GpuVector&&) noexcept = default;
    GpuVector& operator=(GpuVector&&) noexcept = default;

    void clear() { m_values.clear(); }
    void reserve(std::size_t count, const GraphicsDevice& device) {
        if (count <= m_capacity) {
            return;
        }

        constexpr auto max_capacity =
            std::numeric_limits<uint32>::max() / sizeof(T);
        if (count > max_capacity) {
            fatal("GpuVector exceeds the maximum upload size");
        }

        const auto power_of_two_limit = std::bit_floor(max_capacity);
        const auto new_capacity =
            count > power_of_two_limit ? count : std::bit_ceil(count);
        m_buffer = device.create_buffer(
            BufferDescription {
                .size = new_capacity * sizeof(T),
                .usages = m_usages,
            }
        );
        m_capacity = new_capacity;
        ++m_buffer_revision;
    }

    void upload(const GraphicsDevice& device, const RenderQueue& queue) {
        if (m_values.empty()) {
            return;
        }
        reserve(m_values.size(), device);
        queue.write_buffer(
            m_buffer,
            0,
            m_values.data(),
            m_values.size() * sizeof(T)
        );
    }

    void push_back(const T& value) { m_values.push_back(value); }
    void push_back(T&& value) { m_values.push_back(std::move(value)); }

    [[nodiscard]] bool empty() const { return m_values.empty(); }
    [[nodiscard]] std::size_t size() const { return m_values.size(); }
    [[nodiscard]] std::size_t capacity() const { return m_capacity; }
    [[nodiscard]] const T& operator[](std::size_t index) const {
        return m_values[index];
    }
    [[nodiscard]] T& operator[](std::size_t index) { return m_values[index]; }
    [[nodiscard]] std::span<const T> values() const { return m_values; }
    [[nodiscard]] const std::shared_ptr<Buffer>& buffer() const {
        return m_buffer;
    }
    [[nodiscard]] uint64 buffer_revision() const { return m_buffer_revision; }

  private:
    std::vector<T> m_values;
    std::shared_ptr<Buffer> m_buffer;
    BitFlags<BufferUsages> m_usages;
    std::size_t m_capacity {0};
    uint64 m_buffer_revision {0};
};

} // namespace ets
