#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string_view>
#include <webgpu/webgpu.h>

namespace fei {

class MipmapGeneratorWebGpu;

struct WebGpuDeviceStateDescription {
    WGPUInstance instance {nullptr};
    WGPUSurface compatible_surface {nullptr};
    WGPUFeatureLevel feature_level {WGPUFeatureLevel_Core};
    bool allow_compatibility_fallback {false};
};

class WebGpuDeviceState {
  public:
    explicit WebGpuDeviceState(WebGpuDeviceStateDescription desc = {});
    ~WebGpuDeviceState();

    WebGpuDeviceState(const WebGpuDeviceState&) = delete;
    WebGpuDeviceState& operator=(const WebGpuDeviceState&) = delete;
    WebGpuDeviceState(WebGpuDeviceState&&) = delete;
    WebGpuDeviceState& operator=(WebGpuDeviceState&&) = delete;

    [[nodiscard]] WGPUInstance instance() const { return m_instance; }
    [[nodiscard]] WGPUAdapter adapter() const { return m_adapter; }
    [[nodiscard]] WGPUDevice device() const { return m_device; }
    [[nodiscard]] WGPUQueue queue() const { return m_queue; }
    [[nodiscard]] std::size_t uniform_buffer_offset_alignment() const {
        return m_uniform_buffer_offset_alignment;
    }
    [[nodiscard]] std::mutex& queue_mutex() const { return m_queue_mutex; }
    [[nodiscard]] MipmapGeneratorWebGpu& mipmap_generator() const;

    void poll(bool wait = false) const;

  private:
    WGPUInstance m_instance {nullptr};
    WGPUAdapter m_adapter {nullptr};
    WGPUDevice m_device {nullptr};
    WGPUQueue m_queue {nullptr};
    std::size_t m_uniform_buffer_offset_alignment {256};
    mutable std::mutex m_queue_mutex;
    mutable std::once_flag m_mipmap_generator_once;
    mutable std::unique_ptr<MipmapGeneratorWebGpu> m_mipmap_generator;
};

WGPUInstance create_webgpu_instance();
void push_webgpu_error_scope(const WebGpuDeviceState& state);
void check_webgpu_error_scope(
    const WebGpuDeviceState& state,
    std::string_view operation
);

} // namespace fei
