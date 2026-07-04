#pragma once
#include "ecs/world.hpp"
#include "graphics/command_buffer.hpp"

namespace fei {

class RenderCommand {
  public:
    virtual ~RenderCommand() = default;
    virtual void prepare(World& world) {}
    virtual void execute(World& world, CommandBuffer& command_buffer) = 0;
};

} // namespace fei
