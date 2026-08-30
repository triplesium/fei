#include "script_type_values.hpp"

#include "scripting/detail/exported_type.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <Luau/Parser.h>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ets::lsp {
namespace {

using Luau::LanguageServer::Plugin::TextEdit;

class SourceLines final {
  public:
    explicit SourceLines(std::string_view source) : m_source(source) {
        m_offsets.push_back(0);
        for (std::size_t index = 0; index < source.size(); ++index) {
            if (source[index] == '\n') {
                m_offsets.push_back(index + 1);
            }
        }
    }

    [[nodiscard]] std::string_view text(const Luau::Location& location) const {
        const auto begin = offset(location.begin);
        const auto end = offset(location.end);
        if (begin > end || end > m_source.size()) {
            return {};
        }
        return m_source.substr(begin, end - begin);
    }

  private:
    [[nodiscard]] std::size_t offset(const Luau::Position& position) const {
        if (position.line >= m_offsets.size()) {
            return m_source.size();
        }
        return std::min(
            m_offsets[position.line] + position.column,
            m_source.size()
        );
    }

    std::string_view m_source;
    std::vector<std::size_t> m_offsets;
};

[[nodiscard]] bool valid_identifier(std::string_view value) {
    if (value.empty() ||
        (std::isalpha(static_cast<unsigned char>(value.front())) == 0 &&
         value.front() != '_')) {
        return false;
    }
    if (!std::ranges::all_of(value.substr(1), [](char character) {
            const auto value = static_cast<unsigned char>(character);
            return std::isalnum(value) != 0 || character == '_';
        })) {
        return false;
    }
    static constexpr std::array keywords {
        "and",    "break",  "continue", "do",   "else",     "elseif",
        "end",    "export", "false",    "for",  "function", "if",
        "in",     "local",  "nil",      "not",  "or",       "repeat",
        "return", "then",   "true",     "type", "until",    "while",
    };
    return std::ranges::find(keywords, value) == keywords.end();
}

[[nodiscard]] std::string quoted_string(std::string_view value) {
    std::string result {'"'};
    for (const char character : value) {
        switch (character) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result.push_back(character);
                break;
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] std::string type_property(std::string_view name) {
    if (valid_identifier(name)) {
        return std::string {name};
    }
    return '[' + quoted_string(name) + ']';
}

[[nodiscard]] std::string record_declaration(
    const Luau::AstStatTypeAlias& alias,
    const Luau::AstTypeTable& table,
    const SourceLines& source
) {
    const std::string name {alias.name.value};
    const std::string initializer = "__ets_lsp_" + name + "_initializer";
    std::string result;
    result += "\ntype " + initializer + " = {\n";
    for (const auto& property : table.props) {
        result += "    " + type_property(property.name.value) + ": (";
        result += source.text(property.type->location);
        result += ")?,\n";
    }
    result += "}\nexport local " + name + ": TypeToken<" + name + "> & {\n";
    result += "    new: (() -> " + name + ") & ((values: " + initializer +
              ") -> " + name + "),\n";
    result += "} & (() -> " + name + ") & ((values: " + initializer + ") -> " +
              name + ") = nil :: any\n";
    return result;
}

[[nodiscard]] std::string string_union_declaration(
    const Luau::AstStatTypeAlias& alias,
    const std::vector<std::string>& values
) {
    const std::string name {alias.name.value};
    std::string result = "\nexport local " + name + ": {\n";
    std::unordered_set<std::string> emitted;
    for (const auto& value : values) {
        if (emitted.insert(value).second) {
            result += "    " + type_property(value) + ": " + name + ",\n";
        }
    }
    result += "} = nil :: any\n";
    return result;
}

} // namespace

std::vector<TextEdit> script_type_value_edits(std::string_view source) {
    Luau::Allocator allocator;
    Luau::AstNameTable names {allocator};
    const auto parsed =
        Luau::Parser::parse(source.data(), source.size(), names, allocator);
    if (parsed.root == nullptr) {
        return {};
    }

    const SourceLines source_lines {source};
    std::vector<TextEdit> edits;
    for (const Luau::AstStat* statement : parsed.root->body) {
        const auto* alias = statement->as<Luau::AstStatTypeAlias>();
        if (alias == nullptr || !alias->exported || alias->generics.size != 0 ||
            alias->genericPacks.size != 0) {
            continue;
        }
        if (!detail::luau_schema::analyze_exported_type(*alias)
                 .runtime_compatible()) {
            continue;
        }

        auto shape = detail::luau_schema::classify_exported_type(*alias->type);
        std::string declaration;
        if (shape.kind == detail::luau_schema::ExportedTypeKind::Record) {
            declaration = record_declaration(
                *alias,
                *alias->type->as<Luau::AstTypeTable>(),
                source_lines
            );
        } else if (
            shape.kind == detail::luau_schema::ExportedTypeKind::StringUnion
        ) {
            declaration = string_union_declaration(*alias, shape.string_values);
        }
        if (!declaration.empty()) {
            edits.push_back(
                TextEdit {
                    .range = {alias->location.end, alias->location.end},
                    .newText = std::move(declaration),
                }
            );
        }
    }
    return edits;
}

} // namespace ets::lsp
