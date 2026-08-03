#include "rendering/render_frame.hpp"

#include "base/log.hpp"

#include <utility>

namespace fei {

bool RenderFrameContext::begin(const GraphicsDevice& device) {
    if (m_state == State::Recording || m_state == State::Skipped) {
        error("RenderFrameContext::begin called while already recording");
        return false;
    }

    m_command_buffer = device.create_command_buffer();
    if (!m_command_buffer) {
        m_state = State::Skipped;
        error("GraphicsDevice returned null command buffer for render frame");
        return false;
    }
    m_command_buffer->begin();
    m_state = State::Recording;
    return true;
}

std::shared_ptr<CommandBuffer> RenderFrameContext::finish() {
    if (m_state == State::Skipped) {
        m_state = State::Finished;
        return nullptr;
    }
    if (m_state != State::Recording || !m_command_buffer) {
        error("RenderFrameContext::finish called while not recording");
        return nullptr;
    }
    m_command_buffer->end();
    m_state = State::Finished;
    return std::exchange(m_command_buffer, nullptr);
}

void begin_render_frame(
    ResRO<GraphicsDevice> device,
    ResRW<RenderFrameContext> context
) {
    (void)context->begin(*device);
}

void submit_render_frame(
    ResRO<GraphicsDevice> device,
    ResRW<RenderFrameContext> context
) {
    auto command_buffer = context->finish();
    if (command_buffer) {
        device->submit_commands(std::move(command_buffer));
    }
}

} // namespace fei
