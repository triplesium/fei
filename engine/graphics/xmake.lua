includes("core")
if is_plat("wasm") then
    includes("backends/webgpu")
    includes("platform/webgpu_browser")
else
    includes("backends/opengl")
    includes("backends/vulkan")
    includes("backends/webgpu")
    includes("platform/opengl_glfw")
    includes("platform/vulkan_glfw")
    includes("platform/webgpu_glfw")
end
