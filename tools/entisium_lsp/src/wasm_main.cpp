#include "definition_index.hpp"
#include "entisium_platform.hpp"
#include "Flags.hpp"
#include "LSP/Client.hpp"
#include "LSP/LanguageServer.hpp"
#include "Luau/Common.h"
#include "Luau/ExperimentalFlags.h"
#include "wasm_transport.hpp"

#include <cstdlib>
#include <cstring>
#include <emscripten/emscripten.h>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

LUAU_FASTFLAG(LuauSolverV2)

namespace {

constexpr const char* definitions_index =
    "/entisium/luau-definitions/index.json";

class EntisiumClient final : public LSPClient {
  public:
    explicit EntisiumClient(std::unique_ptr<Transport> transport) :
        LSPClient(std::move(transport)) {}

    [[nodiscard]] std::unique_ptr<LSPPlatform> createPlatform(
        const ClientConfiguration&,
        WorkspaceFileResolver* file_resolver,
        WorkspaceFolder* workspace_folder
    ) override {
        return std::make_unique<ets::lsp::EntisiumPlatform>(
            file_resolver,
            workspace_folder
        );
    }
};

void enable_language_features() {
    for (auto* flag = Luau::FValue<bool>::list; flag != nullptr;
         flag = flag->next) {
        if (std::strncmp(flag->name, "Luau", 4) == 0 &&
            !Luau::isAnalysisFlagExperimental(flag->name)) {
            flag->value = true;
        }
    }
    FFlag::LuauSolverV2.value = true;
    applyRequiredFlags();
}

struct Runtime {
    ets::lsp::WasmTransport* transport {nullptr};
    std::unique_ptr<EntisiumClient> client;
    std::unique_ptr<LanguageServer> server;
    std::thread input_thread;
    std::string error;
    bool started {false};

    int start() {
        if (started) {
            return 0;
        }
        try {
            enable_language_features();
            auto wasm_transport = std::make_unique<ets::lsp::WasmTransport>();
            transport = wasm_transport.get();
            client =
                std::make_unique<EntisiumClient>(std::move(wasm_transport));
            client->globalConfig.platform.type = LSPPlatformConfig::Standard;
            client->globalConfig.sourcemap.enabled = false;
            client->globalConfig.types.roblox = false;

            auto definitions =
                ets::lsp::load_definition_index(definitions_index);
            client->definitionsFiles = std::move(definitions.definition_files);
            server = std::make_unique<LanguageServer>(
                client.get(),
                std::move(definitions.base_config)
            );
            input_thread = std::thread([this] {
                server->processInputLoop();
            });
            input_thread.detach();
            started = true;
            return 0;
        } catch (const std::exception& exception) {
            error = exception.what();
            return 1;
        }
    }
};

Runtime runtime;

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int ets_lsp_start() {
    return runtime.start();
}

EMSCRIPTEN_KEEPALIVE void ets_lsp_send(const char* json) {
    if (runtime.transport != nullptr && json != nullptr) {
        runtime.transport->push(json);
    }
}

EMSCRIPTEN_KEEPALIVE char* ets_lsp_take_output() {
    if (runtime.transport == nullptr) {
        return nullptr;
    }
    auto output = runtime.transport->pop_output();
    if (!output) {
        return nullptr;
    }
    auto* result = static_cast<char*>(std::malloc(output->size() + 1));
    if (result == nullptr) {
        return nullptr;
    }
    std::memcpy(result, output->data(), output->size());
    result[output->size()] = '\0';
    return result;
}

EMSCRIPTEN_KEEPALIVE void ets_lsp_free(void* pointer) {
    std::free(pointer);
}

EMSCRIPTEN_KEEPALIVE const char* ets_lsp_last_error() {
    return runtime.error.c_str();
}

} // extern "C"

int main() {
    Luau::assertHandler() = [](const char* expression,
                               const char* file,
                               const int line,
                               const char*) -> int {
        std::cerr << file << '(' << line
                  << "): ASSERTION FAILED: " << expression << '\n';
        return 1;
    };
    return 0;
}
