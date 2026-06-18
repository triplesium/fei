#include "graphics_vulkan/graphics_device.hpp"

#include "base/log.hpp"
#include "graphics_vulkan/resource.hpp"
#include "graphics_vulkan/shader_module.hpp"

#include <exception>
#include <memory>
#include <string_view>

namespace fei {

namespace {

[[noreturn]] void vulkan_not_implemented(std::string_view name) {
    fatal("GraphicsDeviceVulkan::{} is not implemented yet", name);
    std::terminate();
}

} // namespace

std::shared_ptr<ShaderModule>
GraphicsDeviceVulkan::create_shader_module(const ShaderDescription& desc) {
    return std::make_shared<ShaderVulkan>(desc);
}

std::shared_ptr<Buffer>
GraphicsDeviceVulkan::create_buffer(const BufferDescription&) {
    vulkan_not_implemented("create_buffer");
}

std::shared_ptr<Texture>
GraphicsDeviceVulkan::create_texture(const TextureDescription&) {
    vulkan_not_implemented("create_texture");
}

std::shared_ptr<TextureView>
GraphicsDeviceVulkan::create_texture_view(const TextureViewDescription&) {
    vulkan_not_implemented("create_texture_view");
}

std::shared_ptr<CommandBuffer> GraphicsDeviceVulkan::create_command_buffer() {
    vulkan_not_implemented("create_command_buffer");
}

std::shared_ptr<Pipeline>
GraphicsDeviceVulkan::create_render_pipeline(const RenderPipelineDescription&) {
    vulkan_not_implemented("create_render_pipeline");
}

std::shared_ptr<Pipeline> GraphicsDeviceVulkan::create_compute_pipeline(
    const ComputePipelineDescription&
) {
    vulkan_not_implemented("create_compute_pipeline");
}

std::shared_ptr<Framebuffer>
GraphicsDeviceVulkan::create_framebuffer(const FramebufferDescription&) {
    vulkan_not_implemented("create_framebuffer");
}

std::shared_ptr<ResourceLayout> GraphicsDeviceVulkan::create_resource_layout(
    const ResourceLayoutDescription& desc
) {
    return std::make_shared<ResourceLayoutVulkan>(desc);
}

std::shared_ptr<ResourceSet>
GraphicsDeviceVulkan::create_resource_set(const ResourceSetDescription& desc) {
    return std::make_shared<ResourceSetVulkan>(desc);
}

std::shared_ptr<Sampler>
GraphicsDeviceVulkan::create_sampler(const SamplerDescription&) {
    vulkan_not_implemented("create_sampler");
}

void GraphicsDeviceVulkan::submit_commands(std::shared_ptr<CommandBuffer>) {
    vulkan_not_implemented("submit_commands");
}

void GraphicsDeviceVulkan::update_texture(
    std::shared_ptr<Texture>,
    const void*,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t
) {
    vulkan_not_implemented("update_texture");
}

void GraphicsDeviceVulkan::update_buffer(
    std::shared_ptr<Buffer>,
    std::uint32_t,
    const void*,
    std::uint32_t
) {
    vulkan_not_implemented("update_buffer");
}

MappedResource
GraphicsDeviceVulkan::map(std::shared_ptr<MappableResource>, MapMode) {
    vulkan_not_implemented("map");
}

void GraphicsDeviceVulkan::unmap(std::shared_ptr<MappableResource>) {
    vulkan_not_implemented("unmap");
}

std::shared_ptr<Framebuffer> GraphicsDeviceVulkan::main_framebuffer() {
    vulkan_not_implemented("main_framebuffer");
}

} // namespace fei
