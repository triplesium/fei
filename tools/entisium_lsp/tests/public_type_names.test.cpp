#include "public_type_names.hpp"

#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/Error.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/ToString.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
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
    };

    Fixture() {
        configs.defaultConfig.mode = Luau::Mode::Strict;
        Luau::registerBuiltinGlobals(frontend, frontend.globals);
        const auto loaded = frontend.loadDefinitionFile(
            frontend.globals,
            frontend.globals.globalScope,
            R"(
                declare extern type __Entisium_ets_Transform2d with
                    position: number
                end
                export type Transform2d = __Entisium_ets_Transform2d

                declare extern type ThirdPartyInternal with
                end
                export type ThirdParty = ThirdPartyInternal
            )",
            "@entisium",
            false
        );
        REQUIRE(loaded.success);
    }
};

[[nodiscard]] Luau::TypeId exported_type(Fixture& fixture, const char* name) {
    const auto type = fixture.frontend.globals.globalScope->lookupType(name);
    REQUIRE(type.has_value());
    return type->type;
}

} // namespace

TEST_CASE(
    "Entisium extern types use their public exported names",
    "[lsp][types][display]"
) {
    Fixture fixture;
    const auto transform = exported_type(fixture, "Transform2d");
    CHECK(Luau::toString(transform) == "__Entisium_ets_Transform2d");

    ets::lsp::apply_public_type_names(fixture.frontend.globals);

    CHECK(Luau::toString(transform) == "Transform2d");
}

TEST_CASE(
    "public type names apply recursively to inferred types",
    "[lsp][types][display]"
) {
    Fixture fixture;
    ets::lsp::apply_public_type_names(fixture.frontend.globals);
    fixture.files.sources.emplace(
        "nested",
        "local values = {} :: { Transform2d }\nreturn values\n"
    );

    const auto result = fixture.frontend.check("nested");
    REQUIRE(result.errors.empty());
    const auto module = fixture.frontend.moduleResolver.getModule("nested");
    REQUIRE(module != nullptr);
    const auto return_type = module->returnType;

    CHECK(Luau::toString(return_type).find("Transform2d") != std::string::npos);
    CHECK(Luau::toString(return_type).find("__Entisium_") == std::string::npos);
}

TEST_CASE(
    "Luau diagnostics use public Entisium type names",
    "[lsp][types][diagnostics]"
) {
    Fixture fixture;
    ets::lsp::apply_public_type_names(fixture.frontend.globals);
    fixture.files.sources.emplace(
        "diagnostic",
        "local value: Transform2d = \"invalid\"\n"
    );

    const auto result = fixture.frontend.check("diagnostic");
    REQUIRE(result.errors.size() == 1);
    const auto message = Luau::toString(result.errors.front());

    CHECK(message.find("Transform2d") != std::string::npos);
    CHECK(message.find("__Entisium_") == std::string::npos);
}

TEST_CASE(
    "non-Entisium extern type names are preserved",
    "[lsp][types][display]"
) {
    Fixture fixture;
    const auto third_party = exported_type(fixture, "ThirdParty");

    ets::lsp::apply_public_type_names(fixture.frontend.globals);

    CHECK(Luau::toString(third_party) == "ThirdPartyInternal");
}
