#include "base/log.hpp"
#include "project/project.hpp"
#include "runtime_host/application.hpp"

#include <utility>

using namespace fei;

int main(int argc, char** argv) {
    if (argc < 2) {
        error("Usage: fei-runtime-host <path-to-project.yaml>");
        return 1;
    }

    auto project = Project::load(argv[1]);
    if (!project) {
        error(
            "Failed to load project '{}': {}",
            project.error().path.string(),
            project.error().message
        );
        return 1;
    }

    runtime_host::RuntimeHostApplication application(std::move(*project));
    application.run();
    return 0;
}
