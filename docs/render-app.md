# Render App Architecture

This page documents the renderer's internal ownership and threading contract. It is aimed at rendering, backend, and platform work rather than basic sample setup.

The renderer runs in a labeled `SubApp` with its own ECS `World`. This follows the same high-level boundary as Bevy's `SubApp`, `RenderApp`, extraction, and main/render entity synchronization:

```text
Main World startup -> initial RenderExtract -> Render World RenderStartup
    -> Main World update
    -> legacy main-world Render* schedules
    -> RenderExtract systems read the Main World
    -> Render World RenderPrepare ... RenderLast
    -> transfer selected outputs back to the Main World
```

`RenderingPlugin` installs the `RenderApp`. Rendering plugins must register render systems explicitly on it instead of relying on the schedule name:

```cpp
void MyRenderPlugin::setup(App& app) {
    if (!app.has_plugin<RenderingPlugin>()) {
        fatal("MyRenderPlugin requires RenderingPlugin");
    }

    add_extract_component<MyRenderInput>(app);
    app.sub_app<RenderApp>()
        .add_resource(MyRenderState {})
        .add_systems(
            RenderUpdate,
            prepare_my_state |
                in_set<RenderingSystems::PrepareResources>()
        );
}
```

One-time GPU initialization belongs to `RenderStartup`. The Main World's `PreStartUp` and `StartUp` schedules finish first, followed by one initial `RenderExtract`, so extracted asset handles and application configuration are available during render initialization:

```cpp
app.sub_app<RenderApp>()
    .add_resource(MyRenderState {})
    .add_systems(RenderStartup, initialize_my_gpu_state);
```

`add_extract_component<T>` copies `T` from matching main-world entities every frame through the `RenderExtract` schedule. Registering an extracted component also makes it require `SyncToRenderWorld`. Entity synchronization establishes a stable `RenderEntity` component in the Main World and `MainEntity` component in the Render World before extraction begins.

Entity synchronization and component extraction have separate lifetimes. Removing an extracted component removes its Render World copy but retains the linked render entity. Removing `SyncToRenderWorld`, or despawning the main entity, despawns the linked render entity. Because the ECS does not yet expose component lifecycle observers, required markers and stale links are currently found by one centralized archetype scan before extraction.

Custom extraction is an ordinary Render World system. Wrap read-only system parameters in `Extract<P>` to obtain them from the Main World:

```cpp
void extract_cameras(
    Extract<Query<const RenderEntity, const Camera, const GlobalTransform>>
        cameras,
    Commands commands
) {
    for (const auto& [render_entity, camera, transform] : cameras.get()) {
        commands.entity(render_entity.id()).add(
            ExtractedCamera::from(camera, transform)
        );
    }
}

app.sub_app<RenderApp>().add_systems(RenderExtract, extract_cameras);
```

Only read-only parameters such as `ResRO<T>` and queries containing const components are accepted by `Extract<P>`. Commands and unwrapped parameters still target the Render World. The Main World borrow exists only while the `RenderExtract` schedule is running.

Each static system owns its own deferred command buffer, so systems that use `Commands` are not scheduler barriers. After a batch completes, buffers are merged in schedule order. `RenderExtract` deliberately leaves the merged commands pending: the Render App applies them on its execution thread before `RenderPrepare`. With a threaded runner this keeps Render World mutation on the Render Worker; extract systems must not expect their queued changes to become visible during the same `RenderExtract` run. Dynamic and Lua command parameters remain exclusive because their reflected APIs can inspect the World while queuing changes.

Use `add_extract_resource<T>` for application-facing configuration. By default it value-copies a changed Main World `T` into a Render World `T`; duplicate registrations are ignored:

```cpp
app.add_resource(MyRenderSettings {});
add_extract_resource<MyRenderSettings>(app);
```

Source and target types can differ by specializing `ExtractResource<Target>`:

```cpp
template<>
struct ExtractResource<ExtractedCameraSettings> {
    using Source = CameraSettings;

    static ExtractedCameraSettings extract_resource(const Source& source) {
        return ExtractedCameraSettings::from(source);
    }
};
```

The target is inserted on its first extraction and assigned only when the Main World source resource changes. `DeferredPresentSettings` and `VxgiConfig` use this path, so render systems read Render World-local configuration snapshots.

`RenderAssetPlugin` also performs source-asset collection in `RenderExtract`. `Assets<T>::snapshot` returns a `shared_ptr<const T>`, and pending preparation retains that ownership even if the asset is unloaded from the Main World. Asset and event resources are optional, which keeps minimal RenderApp configurations valid and preserves pending retries when a source store is temporarily absent. Calling `Assets<T>::modify` while a snapshot is alive uses copy-on-write for copyable assets, so the published snapshot does not change. Snapshotted non-copyable assets must be replaced with a newly loaded value instead of being modified in place.

Shaders use a specialized extraction path because runtime Slang compilation also consumes imported source files. `extract_shaders` publishes immutable `Assets<Shader>` snapshots into `ShaderCache` and captures every registered `.slang` source root into a `ShaderSourceSnapshot`. Path-based shader requests resolve against that Render World-owned snapshot instead of calling the Main World `AssetServer`, and Slang imports are served by the same snapshot through its virtual file system. A changed asset invalidates only its variants; a changed source-file snapshot invalidates file-backed variants before the next render update.

Long-lived services that are safe to share by const access use an explicit read-only reference rather than extraction:

```cpp
app.sub_app<RenderApp>().add_readonly_resource_ref(
    app.resource<MyRenderService>()
);
```

The referenced resource must outlive the Render App and must not be replaced while it is installed. Render systems can request it with `ResRO<T>`; mutable resource access is rejected. `RenderingPlugin` uses this path for `GraphicsDevice`.

`add_render_to_main_component<T>` is the narrow return channel for asynchronous results such as captured frames. It moves matching components back after the render schedules finish. Normal renderer data should remain one-way.

## Threading contract

`App` drives each SubApp through a `SubAppRunner`. `RenderingPlugin` installs an `InlineRenderRunner` only when the selected graphics backend has not already created the Render App. A backend can therefore establish its required execution policy before rendering resources and schedules are configured.

Applications and backend integration plugins can select another execution policy by calling `install_render_app` with a `RenderRunnerFactory` before installing `RenderingPlugin`. The factory receives the fully configured Render SubApp and transfers its ownership to the selected runner.

`ThreadedRenderRunner` is the pipelined implementation used by every graphics backend. It transfers the entire Render SubApp over two capacity-one channels, so only the main thread or the render worker can access the Render World at a time. `RenderStartup` and the render schedules run on the worker. At the next frame boundary the main thread waits for the previous frame, applies render-to-main outputs, runs extraction, and submits the next frame. Worker exceptions are rethrown at this synchronization point, and shutdown always drains and joins the worker.

`SubAppRunner::run_on_execution_thread` is the synchronous bootstrap boundary. The inline runner invokes the task immediately; the threaded runner transfers the SubApp to its worker, invokes the task there, and returns the SubApp before configuration continues. Exceptions cross the same result channel. This lets a graphics backend create thread-affine device/runtime state without exposing backend rules to `App` or the Render schedules.

During threaded shutdown, the runner transfers the SubApp to the worker one final time and destroys it there before joining. Consequently Render World resources are destroyed on the same execution thread used by bootstrap and rendering. Main World services and the `MainThreadExecutor` remain alive, and main-thread cleanup requests continue to be serviced until that destruction completes.

The threaded runner also installs a read-only `MainThreadExecutor` resource in the Render World. A render system can call `execute` for platform operations that must run on the application thread. Startup, frame synchronization, direct Render App access, and shutdown pump this executor while waiting for the worker, so a render task and the application thread cannot deadlock each other.

Calling `App::sub_app<RenderApp>()` while a threaded frame is running is also a synchronization point. It waits until the Render SubApp returns and keeps it on the caller until the next render update. Runtime code should therefore prefer the extraction and render-to-main channels instead of direct Render World access.

The Render World owns its entities, commands queue, schedules, render-generated components, pipeline and shader caches, frame and queue state, render resource-set caches, view and mesh uniform state, visibility state, rendering defaults, and `RenderAssets<T>`. PBR resources that are created from those caches are initialized in `RenderStartup` and also live in the Render World.

The Render World has no resource fallback to the Main World. Application-facing values arrive through `Extract<T>` or `ExtractResource<T>`, while stable shared services must be registered explicitly as read-only references. Shader source and import snapshots, shader compiler services, Sprite renderer state, ImGui renderer state, and PBR GPU caches are Render World-owned. `ShaderCache` no longer stores references to the Main World's `AssetServer` or `Assets<Shader>`.

The OpenGL, Vulkan, and WebGPU GLFW integrations each have one supported path: `RenderingPlugin` selects `ThreadedRenderRunner` whenever a graphics bootstrap is installed. GPU device state, presentation, and destruction stay on that worker. Unit and headless Render Apps can still select either runner directly when they do not install a platform bootstrap.

## Graphics backend runtime contract

`GraphicsBackendBootstrap` represents platform preparation completed before a graphics device exists. Its `initialize` method is called on the thread that will own the returned `GraphicsRuntime`. A runtime exposes the backend-neutral device, optional presentation target, resize, flush, and present operations. This keeps OpenGL context transfer, Vulkan queue ownership, and WebGPU device polling outside Render App scheduling code.

OpenGL, Vulkan, and WebGPU publish bootstraps that create all GPU-owned state on the Render Worker. Their capabilities are explicit:

| Backend | Main-thread preparation | Extra contract |
| --- | --- | --- |
| OpenGL | GLFW window with an unbound context | Worker makes the context current, loads GLAD, presents, destroys GL resources, then releases the context |
| Vulkan | GLFW window only | Device, surface, swapchain, present and destruction are Render Worker-owned |
| WebGPU | GLFW window only | Surface, device polling, present and destruction are Render Worker-owned |

Graphics bootstraps always require a dedicated Render Worker. `RenderingPlugin` rejects a bootstrap paired with an explicitly installed inline runner, and a bootstrap and the runtime it creates must report identical capabilities. `GraphicsDevice`, `GraphicsRuntime`, and `MainSwapchain` are never borrowed from the Main World. Headless Render Apps without a graphics bootstrap may install a Render World-local test device and select either runner directly.

OpenGL GLFW creates the window on the application thread but does not make its context current there. `OpenGLGlfwBootstrap::initialize` runs on the Render Worker, makes that context current, loads GLAD, and constructs the device and swapchain. OpenGL commands may be recorded by Render World task threads because the backend defers their execution; operations that synchronously touch GL, such as `map` and `unmap`, must be scheduled with `main_thread()` so they execute on the Render Worker's schedule-driving thread. Shutdown destroys the Render World and all GL resources before releasing the context. Only then does the application thread destroy the GLFW window.

Vulkan GLFW publishes a bootstrap as part of normal plugin installation. `RenderingPlugin` selects the Render Worker:

```cpp
app.add_plugin<VulkanGlfwPlugin>().add_plugin<RenderingPlugin>();
```

In this mode the platform plugin creates only the GLFW window, records the required Vulkan extensions, and publishes `GraphicsSurfaceSize`. Rendering initialization invokes `VulkanGlfwBootstrap` through `run_on_execution_thread`; the returned runtime installs `GraphicsRuntime`, `GraphicsDevice`, and `MainSwapchain` only in the Render World. Surface-size snapshots are extracted each frame and resized on the Render execution thread. Sprite setup now validates these resources in the Render World. Rendering DevTools executes graphics-cache requests there, and PBR DevTools creates its texture readback during `RenderStartup`, so none of them require Main World GPU access.

WebGPU GLFW follows the same bootstrap boundary. Its worker creates the WGPU instance, surface, adapter, device, queue, and swapchain, then performs explicit device polling from `GraphicsRuntime::flush`. Surface textures are borrowed frame resources consumed by present: render commands retain their framebuffer through submission, while renderer-owned references are released before the surface is presented. They must never be cached across frames or released as ordinary device-owned textures.

`ImGuiPlugin` keeps platform input and the global ImGui context in the Main World. At the end of the UI frame it copies vertices, indices, draw commands, clip rectangles, texture operations, and display state into a pointer-free `ImGuiFrameSnapshot`. Extraction shares the immutable snapshot with the Render World, where `ImGuiRenderer` and its texture registry are worker-owned. Application images are registered through `ImGuiImages`; their asset IDs are extracted and resolved against `RenderAssets<GpuImage>` on the worker. Arbitrary `ImDrawCallback` functions and ImGui multi-viewport rendering are intentionally not part of this contract.

## Bevy source references

- [`sub_app.rs`](../references/bevy/crates/bevy_app/src/sub_app.rs)
- [`extract_plugin.rs`](../references/bevy/crates/bevy_render/src/extract_plugin.rs)
- [`sync_world.rs`](../references/bevy/crates/bevy_render/src/sync_world.rs)
- [`extract_component.rs`](../references/bevy/crates/bevy_render/src/extract_component.rs)
- [`pipeline_cache.rs`](../references/bevy/crates/bevy_render/src/render_resource/pipeline_cache.rs)
- [`pipelined_rendering.rs`](../references/bevy/crates/bevy_render/src/pipelined_rendering.rs)
