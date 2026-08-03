#pragma once

#include "ecs/system_params.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/graphics_device.hpp"

#include <memory>

namespace fei {

class RenderFrameContext {
  private:
    enum class State {
        Idle,
        Recording,
        Skipped,
        Finished,
    };

    std::shared_ptr<CommandBuffer> m_command_buffer;
    State m_state {State::Idle};

  public:
    [[nodiscard]] bool begin(const GraphicsDevice& device);
    [[nodiscard]] CommandBuffer* command_buffer() const {
        return m_command_buffer.get();
    }
    [[nodiscard]] bool recording() const { return m_command_buffer != nullptr; }
    [[nodiscard]] std::shared_ptr<CommandBuffer> finish();
};

void begin_render_frame(
    ResRO<GraphicsDevice> device,
    ResRW<RenderFrameContext> context
);

void submit_render_frame(
    ResRO<GraphicsDevice> device,
    ResRW<RenderFrameContext> context
);

} // namespace fei
