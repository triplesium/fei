#include "rendering/render_queue.hpp"

#include "base/log.hpp"

#include <cstring>
#include <utility>

namespace fei {

void RenderQueue::write_buffer(
    std::shared_ptr<Buffer> destination,
    uint32 offset,
    const void* data,
    std::size_t size
) const {
    if (!destination) {
        error("RenderQueue::write_buffer received null destination");
        return;
    }
    if (size == 0) {
        return;
    }
    if (data == nullptr) {
        error("RenderQueue::write_buffer received null data");
        return;
    }
    if (static_cast<std::size_t>(offset) > destination->size() ||
        size > destination->size() - offset) {
        error(
            "RenderQueue::write_buffer range [{}, {}) exceeds buffer size {}",
            offset,
            static_cast<std::size_t>(offset) + size,
            destination->size()
        );
        return;
    }

    BufferWrite write {
        .destination = std::move(destination),
        .offset = offset,
        .data = std::vector<std::byte>(size),
    };
    std::memcpy(write.data.data(), data, size);

    std::scoped_lock lock(m_state->mutex);
    m_state->buffer_writes.push_back(std::move(write));
}

std::vector<RenderQueue::BufferWrite> RenderQueue::take_buffer_writes() const {
    std::scoped_lock lock(m_state->mutex);
    return std::exchange(m_state->buffer_writes, {});
}

std::size_t RenderQueue::pending_buffer_writes() const {
    std::scoped_lock lock(m_state->mutex);
    return m_state->buffer_writes.size();
}

void flush_render_queue(
    ResRO<RenderQueue> queue,
    ResRW<RenderFrameContext> frame
) {
    auto* command_buffer = frame->command_buffer();
    if (command_buffer == nullptr) {
        return;
    }

    for (auto& write : queue->take_buffer_writes()) {
        command_buffer->update_buffer(
            std::move(write.destination),
            write.offset,
            write.data.data(),
            write.data.size()
        );
    }
}

} // namespace fei
