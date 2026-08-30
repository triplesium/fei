#include "scripting/compiler.hpp"

#include <array>

namespace ets {

namespace {

constexpr LuauOptimizationPasses only(LuauOptimizationPass pass) {
    auto passes = LuauOptimizationPasses::none();
    return passes.set(pass);
}

constexpr std::array c_pass_descriptors {
    LuauOptimizationPassDescriptor {
        .pass = LuauOptimizationPass::FlattenPropertyPaths,
    },
    LuauOptimizationPassDescriptor {
        .pass = LuauOptimizationPass::ElidePropertyAliases,
        .benefits_from = only(LuauOptimizationPass::FlattenPropertyPaths),
    },
    LuauOptimizationPassDescriptor {
        .pass = LuauOptimizationPass::ReuseQueryUserdata,
    },
    LuauOptimizationPassDescriptor {
        .pass = LuauOptimizationPass::ChunkQueryIteration,
        .benefits_from = only(LuauOptimizationPass::ReuseQueryUserdata),
    },
};

constexpr std::array c_default_pipeline {
    LuauOptimizationPass::ElidePropertyAliases,
    LuauOptimizationPass::FlattenPropertyPaths,
    LuauOptimizationPass::ReuseQueryUserdata,
    LuauOptimizationPass::ChunkQueryIteration,
};

constexpr std::array c_property_pipeline {
    LuauOptimizationPass::ElidePropertyAliases,
    LuauOptimizationPass::FlattenPropertyPaths,
};

constexpr std::array c_query_pipeline {
    LuauOptimizationPass::ReuseQueryUserdata,
    LuauOptimizationPass::ChunkQueryIteration,
};

} // namespace

std::string_view luau_optimization_pass_name(LuauOptimizationPass pass) {
    switch (pass) {
        case LuauOptimizationPass::FlattenPropertyPaths:
            return "flatten-property-paths";
        case LuauOptimizationPass::ElidePropertyAliases:
            return "elide-property-aliases";
        case LuauOptimizationPass::ReuseQueryUserdata:
            return "reuse-query-userdata";
        case LuauOptimizationPass::ChunkQueryIteration:
            return "chunk-query-iteration";
        case LuauOptimizationPass::Count:
            break;
    }
    return "unknown";
}

std::span<const LuauOptimizationPassDescriptor>
luau_optimization_pass_descriptors() {
    return c_pass_descriptors;
}

std::string_view
luau_optimization_pipeline_name(LuauOptimizationPipeline pipeline) {
    switch (pipeline) {
        case LuauOptimizationPipeline::Default:
            return "default";
        case LuauOptimizationPipeline::None:
            return "none";
        case LuauOptimizationPipeline::Property:
            return "property";
        case LuauOptimizationPipeline::Query:
            return "query";
        case LuauOptimizationPipeline::All:
            return "all";
    }
    return "unknown";
}

std::span<const LuauOptimizationPass>
luau_optimization_pipeline_order(LuauOptimizationPipeline pipeline) {
    switch (pipeline) {
        case LuauOptimizationPipeline::Default:
        case LuauOptimizationPipeline::All:
            return c_default_pipeline;
        case LuauOptimizationPipeline::None:
            return {};
        case LuauOptimizationPipeline::Property:
            return c_property_pipeline;
        case LuauOptimizationPipeline::Query:
            return c_query_pipeline;
    }
    return {};
}

LuauOptimizationPasses
luau_optimization_pipeline_passes(LuauOptimizationPipeline pipeline) {
    auto passes = LuauOptimizationPasses::none();
    for (const auto pass : luau_optimization_pipeline_order(pipeline)) {
        passes.set(pass);
    }
    return passes;
}

std::vector<LuauOptimizationDiagnostic>
diagnose_luau_optimization_passes(LuauOptimizationPasses passes) {
    std::vector<LuauOptimizationDiagnostic> diagnostics;
    for (const auto& descriptor : c_pass_descriptors) {
        if (!passes.contains(descriptor.pass)) {
            continue;
        }
        for (const auto& dependency : c_pass_descriptors) {
            if (passes.contains(dependency.pass)) {
                continue;
            }
            if (descriptor.required_passes.contains(dependency.pass)) {
                diagnostics.push_back(
                    LuauOptimizationDiagnostic {
                        .pass = descriptor.pass,
                        .dependency = dependency.pass,
                        .kind = LuauOptimizationDependencyKind::Required,
                    }
                );
            } else if (descriptor.benefits_from.contains(dependency.pass)) {
                diagnostics.push_back(
                    LuauOptimizationDiagnostic {
                        .pass = descriptor.pass,
                        .dependency = dependency.pass,
                        .kind = LuauOptimizationDependencyKind::BenefitsFrom,
                    }
                );
            }
        }
    }
    return diagnostics;
}

} // namespace ets
