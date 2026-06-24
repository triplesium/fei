#include "graphics_opengl/texture_readback.hpp"

#include "base/log.hpp"
#include "graphics/utils.hpp"
#include "graphics_opengl/deferred_resource.hpp"
#include "graphics_opengl/graphics_device.hpp"
#include "graphics_opengl/texture.hpp"
#include "profiling/profiling.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace fei {

namespace {

constexpr uint32 c_readback_channels {4};

class TextureReadbackDisposalOpenGL : public DeferredResourceOpenGL {
  public:
    TextureReadbackDisposalOpenGL(
        std::vector<GLuint> pbos,
        std::vector<GLsync> fences
    ) :
        DeferredResourceOpenGL(true), m_pbos(std::move(pbos)),
        m_fences(std::move(fences)) {}

  private:
    void create_gl_resource() const override {}

    void destroy_gl_resource() override {
        for (auto fence : m_fences) {
            if (fence != nullptr) {
                FEI_GL_CALL(glDeleteSync(fence));
            }
        }
        m_fences.clear();

        if (!m_pbos.empty()) {
            FEI_GL_CALL(glDeleteBuffers(
                static_cast<GLsizei>(m_pbos.size()),
                m_pbos.data()
            ));
            m_pbos.clear();
        }
    }

    std::vector<GLuint> m_pbos;
    std::vector<GLsync> m_fences;
};

std::size_t rgba8_byte_count(uint32 width, uint32 height) {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
           c_readback_channels;
}

bool is_supported_async_request(const TextureReadbackRequest& request) {
    return request.texture &&
           request.texture->type() == TextureType::Texture2D &&
           request.texture->format() == PixelFormat::Rgba8Unorm &&
           request.output_format == PixelFormat::Rgba8Unorm &&
           request.mip_level < request.texture->mip_level() &&
           request.layer == 0 && request.texture->depth() == 1;
}

} // namespace

TextureReadbackOpenGL::TextureReadbackOpenGL(
    std::shared_ptr<OpenGLDeviceState> device_state,
    uint32 max_in_flight
) :
    m_device_state(std::move(device_state)),
    m_state(std::make_shared<OpenGLTextureReadbackState>()) {
    m_state->slots.resize(std::max(max_in_flight, uint32 {1}));
    m_device_state->register_texture_readback(m_state);
}

TextureReadbackOpenGL::~TextureReadbackOpenGL() {
    reset();
}

bool TextureReadbackOpenGL::can_enqueue() const {
    std::scoped_lock lock(m_state->mutex);
    if (m_state->completed_frames.size() >= m_state->slots.size()) {
        return false;
    }
    return std::ranges::any_of(m_state->slots, [](const auto& slot) {
        return !slot.queued && !slot.pending;
    });
}

bool TextureReadbackOpenGL::enqueue(TextureReadbackRequest request) {
    if (!is_supported_async_request(request)) {
        return false;
    }

    auto [width, height, depth] =
        Utils::get_mip_dimensions(request.texture, request.mip_level);
    if (depth != 1) {
        return false;
    }
    auto byte_count = rgba8_byte_count(width, height);

    std::size_t slot_index = 0;
    {
        std::scoped_lock lock(m_state->mutex);
        auto slot_count = m_state->slots.size();
        bool found = false;
        for (std::size_t attempt = 0; attempt < slot_count; ++attempt) {
            auto index = (m_state->next_slot + attempt) % slot_count;
            auto& slot = m_state->slots[index];
            if (!slot.queued && !slot.pending) {
                slot_index = index;
                slot.queued = true;
                slot.pending = false;
                slot.byte_count = byte_count;
                slot.width = width;
                slot.height = height;
                slot.format = request.output_format;
                slot.user_data = request.user_data;
                m_state->next_slot = (index + 1) % slot_count;
                found = true;
                break;
            }
        }

        if (!found) {
            return false;
        }
    }

    m_device_state->enqueue_operation(
        OpenGLPendingTextureReadback {
            .state = m_state,
            .texture = std::move(request.texture),
            .slot_index = slot_index,
            .mip_level = request.mip_level,
        }
    );
    return true;
}

Optional<TextureReadbackFrame> TextureReadbackOpenGL::poll() {
    FEI_PROFILE_SCOPE("OpenGL Texture Readback Poll");
    std::scoped_lock lock(m_state->mutex);
    if (m_state->completed_frames.empty()) {
        return nullopt;
    }

    auto frame = std::move(m_state->completed_frames.front());
    m_state->completed_frames.pop_front();
    return frame;
}

void TextureReadbackOpenGL::reset() {
    std::vector<GLuint> pbos;
    std::vector<GLsync> fences;
    std::scoped_lock lock(m_state->mutex);
    for (auto& slot : m_state->slots) {
        if (slot.fence != nullptr) {
            fences.push_back(slot.fence);
        }
        if (slot.pbo != 0) {
            pbos.push_back(slot.pbo);
        }
        slot = OpenGLTextureReadbackSlot {};
    }
    m_state->completed_frames.clear();
    m_state->next_slot = 0;

    if (!pbos.empty() || !fences.empty()) {
        m_device_state->enqueue_disposal(
            std::make_unique<TextureReadbackDisposalOpenGL>(
                std::move(pbos),
                std::move(fences)
            )
        );
    }
}

void collect_ready_texture_readbacks(
    const std::shared_ptr<OpenGLTextureReadbackState>& state
) {
    FEI_PROFILE_SCOPE("OpenGL Texture Readback Collect");
    if (!state) {
        return;
    }

    std::scoped_lock lock(state->mutex);
    if (state->completed_frames.size() >= state->slots.size()) {
        return;
    }

    for (auto& slot : state->slots) {
        if (!slot.pending || slot.fence == nullptr) {
            continue;
        }

        auto wait_result = FEI_GL_CALL(glClientWaitSync(slot.fence, 0, 0));
        if (wait_result != GL_ALREADY_SIGNALED &&
            wait_result != GL_CONDITION_SATISFIED) {
            continue;
        }

        FEI_GL_CALL(glDeleteSync(slot.fence));
        slot.fence = nullptr;

        auto* mapped =
            static_cast<const byte*>(FEI_GL_CALL(glMapNamedBufferRange(
                slot.pbo,
                0,
                static_cast<GLsizeiptr>(slot.byte_count),
                GL_MAP_READ_BIT
            )));
        if (mapped == nullptr) {
            fatal("OpenGL texture readback PBO mapping failed");
        }

        TextureReadbackFrame frame {
            .data = std::vector<byte>(slot.byte_count),
            .width = slot.width,
            .height = slot.height,
            .depth = 1,
            .format = slot.format,
            .user_data = slot.user_data,
        };
        std::memcpy(frame.data.data(), mapped, frame.data.size());
        FEI_GL_CALL(glUnmapNamedBuffer(slot.pbo));

        slot.pending = false;
        slot.queued = false;
        slot.byte_count = 0;
        slot.width = 0;
        slot.height = 0;
        slot.user_data = 0;

        state->completed_frames.push_back(std::move(frame));
        if (state->completed_frames.size() >= state->slots.size()) {
            return;
        }
    }
}

void execute_texture_readback(const OpenGLPendingTextureReadback& readback) {
    FEI_PROFILE_SCOPE("OpenGL Texture Readback Enqueue");
    auto texture_gl =
        std::dynamic_pointer_cast<TextureOpenGL>(readback.texture);
    if (!texture_gl) {
        fatal("OpenGL texture readback received a non-OpenGL texture");
    }
    texture_gl->ensure_created();

    std::scoped_lock lock(readback.state->mutex);
    if (readback.slot_index >= readback.state->slots.size()) {
        return;
    }

    auto& slot = readback.state->slots[readback.slot_index];
    if (!slot.queued || slot.pending) {
        return;
    }

    if (slot.pbo == 0) {
        FEI_GL_CALL(glCreateBuffers(1, &slot.pbo));
    }
    if (slot.allocated_bytes != slot.byte_count) {
        FEI_GL_CALL(glNamedBufferData(
            slot.pbo,
            static_cast<GLsizeiptr>(slot.byte_count),
            nullptr,
            GL_STREAM_READ
        ));
        slot.allocated_bytes = slot.byte_count;
    }

    FEI_GL_CALL(glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo));
    FEI_GL_CALL(glPixelStorei(GL_PACK_ALIGNMENT, 1));
    FEI_GL_CALL(glGetTextureImage(
        texture_gl->id(),
        static_cast<GLint>(readback.mip_level),
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        static_cast<GLsizei>(slot.byte_count),
        nullptr
    ));
    FEI_GL_CALL(glBindBuffer(GL_PIXEL_PACK_BUFFER, 0));

    slot.fence = FEI_GL_CALL(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
    slot.queued = false;
    slot.pending = true;
}

} // namespace fei
