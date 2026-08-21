#include "graphics_webgpu/context.hpp"

#include "base/log.hpp"
#include "mipmap_generator.hpp"

#ifdef __EMSCRIPTEN__
#    include <emscripten.h>
#else
#    include <thread>
#    include <webgpu/wgpu.h>
#endif
#include <string>
#include <utility>

namespace fei {

namespace {

#ifdef __EMSCRIPTEN__
constexpr auto async_callback_mode = WGPUCallbackMode_AllowSpontaneous;
#else
constexpr auto async_callback_mode = WGPUCallbackMode_AllowProcessEvents;
#endif

std::string to_string(WGPUStringView value) {
    if (value.data == nullptr) {
        return {};
    }
    if (value.length == WGPU_STRLEN) {
        return value.data;
    }
    return {value.data, value.length};
}

void wait_for(WGPUInstance instance, const bool& completed) {
    while (!completed) {
#ifdef __EMSCRIPTEN__
        emscripten_sleep(0);
#else
        wgpuInstanceProcessEvents(instance);
        std::this_thread::yield();
#endif
    }
}

struct AdapterRequest {
    WGPUAdapter adapter {nullptr};
    std::string error;
    bool completed {false};
};

void on_adapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void* userdata1,
    void*
) {
    auto& request = *static_cast<AdapterRequest*>(userdata1);
    if (status == WGPURequestAdapterStatus_Success) {
        request.adapter = adapter;
    } else {
        request.error = to_string(message);
    }
    request.completed = true;
}

struct DeviceRequest {
    WGPUDevice device {nullptr};
    std::string error;
    bool completed {false};
};

void on_device(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void* userdata1,
    void*
) {
    auto& request = *static_cast<DeviceRequest*>(userdata1);
    if (status == WGPURequestDeviceStatus_Success) {
        request.device = device;
    } else {
        request.error = to_string(message);
    }
    request.completed = true;
}

void on_device_lost(
    const WGPUDevice*,
    WGPUDeviceLostReason reason,
    WGPUStringView message,
    void*,
    void*
) {
    error(
        "WebGPU device lost (reason {}): {}",
        static_cast<int>(reason),
        to_string(message)
    );
}

void on_uncaptured_error(
    const WGPUDevice*,
    WGPUErrorType type,
    WGPUStringView message,
    void*,
    void*
) {
    error(
        "WebGPU uncaptured error (type {}): {}",
        static_cast<int>(type),
        to_string(message)
    );
}

#ifndef __EMSCRIPTEN__
struct ErrorScopeResult {
    WGPUPopErrorScopeStatus status {WGPUPopErrorScopeStatus_Force32};
    WGPUErrorType type {WGPUErrorType_NoError};
    std::string message;
    bool completed {false};
};

void on_error_scope(
    WGPUPopErrorScopeStatus status,
    WGPUErrorType type,
    WGPUStringView message,
    void* userdata1,
    void*
) {
    auto& result = *static_cast<ErrorScopeResult*>(userdata1);
    result.status = status;
    result.type = type;
    result.message = to_string(message);
    result.completed = true;
}
#endif

#ifdef __EMSCRIPTEN__
struct BrowserErrorScopeResult {
    std::string operation;
};

void on_browser_error_scope(
    WGPUPopErrorScopeStatus status,
    WGPUErrorType type,
    WGPUStringView message,
    void* userdata1,
    void*
) {
    auto* result = static_cast<BrowserErrorScopeResult*>(userdata1);
    if (status != WGPUPopErrorScopeStatus_Success ||
        type != WGPUErrorType_NoError) {
        auto operation = std::move(result->operation);
        auto error_message = to_string(message);
        delete result;
        fatal("{} failed: {}", operation, error_message);
    }
    delete result;
}
#endif
} // namespace

WGPUInstance create_webgpu_instance() {
    WGPUInstanceDescriptor descriptor {};
    auto instance = wgpuCreateInstance(&descriptor);
    if (instance == nullptr) {
        fatal("Failed to create WebGPU instance");
    }
    return instance;
}

WebGpuDeviceState::WebGpuDeviceState(WebGpuDeviceStateDescription desc) :
    m_instance(
        desc.instance != nullptr ? desc.instance : create_webgpu_instance()
    ) {
    WGPURequestAdapterOptions options {
        .featureLevel = desc.feature_level,
        .powerPreference = WGPUPowerPreference_HighPerformance,
        .compatibleSurface = desc.compatible_surface,
    };

    const auto request_adapter = [this, &options] {
        AdapterRequest request;
        WGPURequestAdapterCallbackInfo callback {
            .mode = async_callback_mode,
            .callback = on_adapter,
            .userdata1 = &request,
        };
        wgpuInstanceRequestAdapter(m_instance, &options, callback);
        wait_for(m_instance, request.completed);
        return request;
    };

    auto adapter_request = request_adapter();
    if (adapter_request.adapter == nullptr &&
        desc.allow_compatibility_fallback &&
        options.featureLevel == WGPUFeatureLevel_Core) {
        warn(
            "Failed to request a core WebGPU adapter ({}); retrying with "
            "compatibility mode",
            adapter_request.error
        );
        options.featureLevel = WGPUFeatureLevel_Compatibility;
        adapter_request = request_adapter();
    }
    if (adapter_request.adapter == nullptr) {
        fatal("Failed to request WebGPU adapter: {}", adapter_request.error);
    }
    m_adapter = adapter_request.adapter;

    WGPULimits adapter_limits {};
    if (wgpuAdapterGetLimits(m_adapter, &adapter_limits) !=
        WGPUStatus_Success) {
        fatal("Failed to query WebGPU adapter limits");
    }

    WGPUFeatureName required_features[1] {};
    std::size_t required_feature_count = 0;
    if (wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_Float32Filterable)) {
        required_features[required_feature_count++] =
            WGPUFeatureName_Float32Filterable;
    }

    WGPUDeviceDescriptor device_descriptor {
        .label = {"fei WebGPU device", WGPU_STRLEN},
        .requiredFeatureCount = required_feature_count,
        .requiredFeatures = required_features,
        .requiredLimits = &adapter_limits,
        .defaultQueue =
            {
                .label = {"fei WebGPU queue", WGPU_STRLEN},
            },
        .deviceLostCallbackInfo =
            {
                .mode = WGPUCallbackMode_AllowSpontaneous,
                .callback = on_device_lost,
            },
        .uncapturedErrorCallbackInfo = {
            .callback = on_uncaptured_error,
        },
    };

    DeviceRequest device_request;
    WGPURequestDeviceCallbackInfo device_callback {
        .mode = async_callback_mode,
        .callback = on_device,
        .userdata1 = &device_request,
    };
    wgpuAdapterRequestDevice(m_adapter, &device_descriptor, device_callback);
    wait_for(m_instance, device_request.completed);
    if (device_request.device == nullptr) {
        fatal("Failed to request WebGPU device: {}", device_request.error);
    }
    m_device = device_request.device;
    m_queue = wgpuDeviceGetQueue(m_device);
    m_uniform_buffer_offset_alignment =
        adapter_limits.minUniformBufferOffsetAlignment;
}

WebGpuDeviceState::~WebGpuDeviceState() {
    m_mipmap_generator.reset();
    if (m_queue != nullptr) {
        wgpuQueueRelease(m_queue);
    }
    if (m_device != nullptr) {
        wgpuDeviceRelease(m_device);
    }
    if (m_adapter != nullptr) {
        wgpuAdapterRelease(m_adapter);
    }
    if (m_instance != nullptr) {
        wgpuInstanceRelease(m_instance);
    }
}

MipmapGeneratorWebGpu& WebGpuDeviceState::mipmap_generator() const {
    std::call_once(m_mipmap_generator_once, [this] {
        m_mipmap_generator = std::make_unique<MipmapGeneratorWebGpu>(*this);
    });
    return *m_mipmap_generator;
}

void WebGpuDeviceState::poll(bool wait) const {
#ifdef __EMSCRIPTEN__
    static_cast<void>(wait);
#else
    wgpuDevicePoll(m_device, wait, nullptr);
    wgpuInstanceProcessEvents(m_instance);
#endif
}

void push_webgpu_error_scope(const WebGpuDeviceState& state) {
    wgpuDevicePushErrorScope(state.device(), WGPUErrorFilter_Validation);
}

void check_webgpu_error_scope(
    const WebGpuDeviceState& state,
    std::string_view operation
) {
#ifdef __EMSCRIPTEN__
    auto* result = new BrowserErrorScopeResult {std::string(operation)};
    WGPUPopErrorScopeCallbackInfo callback {
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = on_browser_error_scope,
        .userdata1 = result,
    };
    wgpuDevicePopErrorScope(state.device(), callback);
#else
    ErrorScopeResult result;
    WGPUPopErrorScopeCallbackInfo callback {
        .mode = async_callback_mode,
        .callback = on_error_scope,
        .userdata1 = &result,
    };
    wgpuDevicePopErrorScope(state.device(), callback);
    wait_for(state.instance(), result.completed);
    if (result.status != WGPUPopErrorScopeStatus_Success ||
        result.type != WGPUErrorType_NoError) {
        fatal("{} failed: {}", operation, result.message);
    }
#endif
}

} // namespace fei
