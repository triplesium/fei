#include "graphics/backend.hpp"

#include <stdexcept>
#include <utility>

namespace fei {

BoxedGraphicsBackendBootstrap::BoxedGraphicsBackendBootstrap(
    std::unique_ptr<GraphicsBackendBootstrap> bootstrap
) : m_bootstrap(std::move(bootstrap)) {
    if (!m_bootstrap) {
        throw std::invalid_argument(
            "BoxedGraphicsBackendBootstrap requires a bootstrap"
        );
    }
}

BoxedGraphicsRuntime::BoxedGraphicsRuntime(
    std::unique_ptr<GraphicsRuntime> runtime
) : m_runtime(std::move(runtime)) {
    if (!m_runtime) {
        throw std::invalid_argument("BoxedGraphicsRuntime requires a runtime");
    }
}

} // namespace fei
