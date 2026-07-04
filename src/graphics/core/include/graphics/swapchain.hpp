#pragma once
#include "base/types.hpp"
#include "graphics/enums.hpp"
#include "graphics/framebuffer.hpp"

#include <memory>

namespace fei {

class Swapchain {
  public:
    virtual ~Swapchain() = default;

    virtual std::shared_ptr<const Framebuffer> framebuffer() const = 0;
    virtual uint32 width() const = 0;
    virtual uint32 height() const = 0;
    virtual PixelFormat color_format() const = 0;
    virtual void resize(uint32 width, uint32 height) = 0;
    virtual void present() const = 0;
};

struct MainSwapchain {
    std::shared_ptr<Swapchain> swapchain;
};

} // namespace fei
