#pragma once

#include "graphics/texture_readback.hpp"
#include "graphics_opengl/utils.hpp"

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace fei {

class OpenGLDeviceState;
struct OpenGLPendingTextureReadback;

struct OpenGLTextureReadbackSlot {
    GLuint pbo {0};
    GLsync fence {nullptr};
    std::size_t allocated_bytes {0};
    std::size_t byte_count {0};
    uint32 width {0};
    uint32 height {0};
    PixelFormat format {PixelFormat::Rgba8Unorm};
    uint64 user_data {0};
    bool queued {false};
    bool pending {false};
};

struct OpenGLTextureReadbackState {
    mutable std::mutex mutex;
    std::vector<OpenGLTextureReadbackSlot> slots;
    std::deque<TextureReadbackFrame> completed_frames;
    std::size_t next_slot {0};
};

class TextureReadbackOpenGL final : public TextureReadback {
  public:
    TextureReadbackOpenGL(
        std::shared_ptr<OpenGLDeviceState> device_state,
        uint32 max_in_flight
    );
    ~TextureReadbackOpenGL() override;

    TextureReadbackOpenGL(const TextureReadbackOpenGL&) = delete;
    TextureReadbackOpenGL& operator=(const TextureReadbackOpenGL&) = delete;

    bool can_enqueue() const override;
    bool enqueue(TextureReadbackRequest request) override;
    Optional<TextureReadbackFrame> poll() override;
    void reset() override;

  private:
    void release_resources();

    std::shared_ptr<OpenGLDeviceState> m_device_state;
    std::shared_ptr<OpenGLTextureReadbackState> m_state;
};

void execute_texture_readback(const OpenGLPendingTextureReadback& readback);
void collect_ready_texture_readbacks(
    const std::shared_ptr<OpenGLTextureReadbackState>& state
);

} // namespace fei
