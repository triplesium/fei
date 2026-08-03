#pragma once

#include "graphics/texture_readback.hpp"

#include <deque>
#include <memory>
#include <mutex>

namespace fei {

class VulkanDeviceState;

class TextureReadbackVulkan final : public TextureReadback {
  private:
    std::shared_ptr<VulkanDeviceState> m_state;
    mutable std::mutex m_mutex;
    std::deque<TextureReadbackFrame> m_completed_frames;
    uint32 m_max_in_flight {1};

  public:
    TextureReadbackVulkan(
        std::shared_ptr<VulkanDeviceState> state,
        uint32 max_in_flight
    );
    ~TextureReadbackVulkan() override = default;

    TextureReadbackVulkan(const TextureReadbackVulkan&) = delete;
    TextureReadbackVulkan& operator=(const TextureReadbackVulkan&) = delete;

    bool can_enqueue() const override;
    bool enqueue(TextureReadbackRequest request) override;
    Optional<TextureReadbackFrame> poll() override;
    void reset() override;
};

} // namespace fei
