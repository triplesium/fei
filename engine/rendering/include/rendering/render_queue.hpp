#pragma once

#include "base/types.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "rendering/render_frame.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

namespace fei {

class RenderQueue {
  private:
    struct BufferWrite {
        std::shared_ptr<Buffer> destination;
        uint32 offset {};
        std::vector<std::byte> data;
    };

    struct State {
        std::mutex mutex;
        std::vector<BufferWrite> buffer_writes;
    };

    std::shared_ptr<State> m_state {std::make_shared<State>()};

    [[nodiscard]] std::vector<BufferWrite> take_buffer_writes() const;

    friend void flush_render_queue(
        ResRO<RenderQueue> queue,
        ResRW<RenderFrameContext> frame
    );

  public:
    void write_buffer(
        std::shared_ptr<Buffer> destination,
        uint32 offset,
        const void* data,
        std::size_t size
    ) const;

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    void
    write_buffer(std::shared_ptr<Buffer> destination, const T& value) const {
        write_buffer(std::move(destination), 0, &value, sizeof(T));
    }

    [[nodiscard]] std::size_t pending_buffer_writes() const;
};

void flush_render_queue(
    ResRO<RenderQueue> queue,
    ResRW<RenderFrameContext> frame
);

} // namespace fei
