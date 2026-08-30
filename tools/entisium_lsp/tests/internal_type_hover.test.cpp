#include "internal_type_hover.hpp"

#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

class MemoryFileResolver final : public Luau::FileResolver {
  public:
    std::unordered_map<Luau::ModuleName, std::string> sources;

    std::optional<Luau::SourceCode>
    readSource(const Luau::ModuleName& name) override {
        const auto found = sources.find(name);
        if (found == sources.end()) {
            return std::nullopt;
        }
        return Luau::SourceCode {
            .source = found->second,
            .type = Luau::SourceCode::Module,
        };
    }
};

struct Fixture {
    MemoryFileResolver files;
    Luau::NullConfigResolver configs;
    Luau::Frontend frontend {
        Luau::SolverMode::New,
        &files,
        &configs,
        {.retainFullTypeGraphs = true},
    };

    Fixture() {
        configs.defaultConfig.mode = Luau::Mode::Strict;
        Luau::registerBuiltinGlobals(frontend, frontend.globals);
        const auto loaded = frontend.loadDefinitionFile(
            frontend.globals,
            frontend.globals.globalScope,
            R"(
                export type TypeToken<T> = {
                    __ets_type: T?,
                }
                export type Handle<T> = {
                    __ets_asset_type: T?,
                }

                declare extern type Image with
                end
                declare extern type Transform2d with
                end
                declare Image: TypeToken<Image>
                declare assets: {
                    load: <T>(type_token: TypeToken<T>) -> Handle<T>,
                }
                declare core: {
                    Transform2d: {
                        __ets_type: Transform2d?,
                        __ets_type_id: number,
                        __ets_type_name: string,
                        new: () -> Transform2d,
                    },
                }
            )",
            "@entisium",
            false
        );
        REQUIRE(loaded.success);
    }
};

[[nodiscard]] Luau::Position
position_of(const std::string_view source, const std::string_view needle) {
    const auto match = source.rfind(needle);
    REQUIRE(match != std::string_view::npos);
    const auto offset = match + (needle.empty() ? 0 : 1);

    Luau::Position position {0, 0};
    for (std::size_t index = 0; index < offset; ++index) {
        if (source[index] == '\n') {
            ++position.line;
            position.column = 0;
        } else {
            ++position.column;
        }
    }
    return position;
}

[[nodiscard]] std::optional<std::string> hover_at(
    Fixture& fixture,
    const std::string& module_name,
    const std::string& source,
    const std::string_view needle
) {
    fixture.files.sources.emplace(module_name, source);
    const auto result = fixture.frontend.check(module_name);
    REQUIRE(result.errors.empty());

    const auto source_module = fixture.frontend.getSourceModule(module_name);
    const auto module = fixture.frontend.moduleResolver.getModule(module_name);
    REQUIRE(source_module != nullptr);
    REQUIRE(module != nullptr);
    const auto position = position_of(source, needle);
    const auto aliases =
        ets::lsp::collect_internal_type_aliases(fixture.frontend.globals);
    return ets::lsp::internal_hover_signature(
        *source_module,
        *module,
        position,
        aliases,
        true
    );
}

[[nodiscard]] std::optional<std::string> function_return_at(
    Fixture& fixture,
    const std::string& module_name,
    const std::string& source,
    const std::string_view needle
) {
    fixture.files.sources.emplace(module_name, source);
    const auto result = fixture.frontend.check(module_name);
    REQUIRE(result.errors.empty());

    const auto source_module = fixture.frontend.getSourceModule(module_name);
    const auto module = fixture.frontend.moduleResolver.getModule(module_name);
    REQUIRE(source_module != nullptr);
    REQUIRE(module != nullptr);
    const auto aliases =
        ets::lsp::collect_internal_type_aliases(fixture.frontend.globals);
    return ets::lsp::internal_function_return_type(
        *source_module,
        *module,
        position_of(source, needle),
        aliases,
        true
    );
}

TEST_CASE(
    "internal local hover recovers aliases lost through inferred returns",
    "[lsp][hover][types]"
) {
    Fixture fixture;
    const std::string source = "local function require_image()\n"
                               "    local loaded = assets.load(Image)\n"
                               "    return loaded\n"
                               "end\n"
                               "local image = require_image()\n";

    const auto hover =
        hover_at(fixture, "inferred-return-hover", source, "image =");

    REQUIRE(hover.has_value());
    CHECK(*hover == "local image: Handle<Image>");
    CHECK(hover->find("__ets_") == std::string::npos);
}

TEST_CASE(
    "empty autocomplete globals do not clear internal aliases",
    "[lsp][hover][types][solver-v2]"
) {
    Fixture fixture;
    ets::lsp::InternalTypeAliases aliases;

    ets::lsp::update_internal_type_aliases(aliases, fixture.frontend.globals);
    REQUIRE_FALSE(aliases.empty());
    const auto retained = aliases;

    ets::lsp::update_internal_type_aliases(
        aliases,
        fixture.frontend.globalsForAutocomplete
    );

    REQUIRE(aliases.size() == retained.size());
    for (std::size_t index = 0; index < aliases.size(); ++index) {
        CHECK(
            aliases[index].marker_property == retained[index].marker_property
        );
        CHECK(aliases[index].alias_name == retained[index].alias_name);
    }
}

TEST_CASE(
    "internal local hover keeps the generic alias and hides marker fields",
    "[lsp][hover][types]"
) {
    Fixture fixture;
    const std::string source = "local image = assets.load(Image)\n";

    const auto hover = hover_at(fixture, "internal-hover", source, "image");

    REQUIRE(hover.has_value());
    CHECK(*hover == "local image: Handle<Image>");
    CHECK(hover->find("__ets_") == std::string::npos);
}

TEST_CASE(
    "ordinary local hover remains owned by luau-lsp",
    "[lsp][hover][types]"
) {
    Fixture fixture;
    const std::string source = "type Point = { x: number }\n"
                               "local point = { x = 1 } :: Point\n"
                               "return point\n";

    CHECK_FALSE(hover_at(fixture, "ordinary-hover", source, "point"));
}

TEST_CASE(
    "type token hover hides internal fields and keeps public constructors",
    "[lsp][hover][types][internal]"
) {
    Fixture fixture;
    const std::string source = "local token = core.Transform2d\n";

    const auto hover =
        hover_at(fixture, "type-token-hover", source, "Transform2d");

    REQUIRE(hover.has_value());
    CHECK(hover->find("new") != std::string::npos);
    CHECK(hover->find("__ets_") == std::string::npos);
    CHECK(hover->find("__type_") == std::string::npos);
}

TEST_CASE(
    "function hover recovers aliases from inferred return types",
    "[lsp][hover][types][return]"
) {
    Fixture fixture;
    const std::string source = "local function require_image()\n"
                               "    local image = assets.load(Image)\n"
                               "    return image\n"
                               "end\n";

    const auto return_type = function_return_at(
        fixture,
        "function-return-hover",
        source,
        "require_image"
    );

    REQUIRE(return_type.has_value());
    CHECK(*return_type == "Handle<Image>");
}

} // namespace
