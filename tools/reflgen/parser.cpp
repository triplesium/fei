#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <clang-c/Index.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace fei::reflgen {
namespace {

struct TranslationUnitContext {
    CXTranslationUnit translation_unit = nullptr;
    std::string header_path;
    std::string comparable_header_path;
};

template<typename Visitor>
void visit_children(CXCursor cursor, Visitor visitor) {
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor parent, CXClientData data) {
            auto& fn = *static_cast<Visitor*>(data);
            return fn(child, parent);
        },
        &visitor
    );
}

[[nodiscard]] std::string clang_string(CXString value) {
    const char* cstr = clang_getCString(value);
    std::string result = cstr ? cstr : "";
    clang_disposeString(value);
    return result;
}

[[nodiscard]] std::string cursor_spelling(CXCursor cursor) {
    return clang_string(clang_getCursorSpelling(cursor));
}

[[nodiscard]] std::string cursor_display_name(CXCursor cursor) {
    return clang_string(clang_getCursorDisplayName(cursor));
}

[[nodiscard]] std::string type_spelling(CXType type) {
    return clang_string(clang_getTypeSpelling(type));
}

[[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

[[nodiscard]] std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

[[nodiscard]] std::filesystem::path
normalized_absolute_path(const std::filesystem::path& path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        absolute = path;
    }
    return absolute.lexically_normal();
}

[[nodiscard]] std::string
generic_absolute_path(const std::filesystem::path& path) {
    return normalized_absolute_path(path).generic_string();
}

[[nodiscard]] std::string comparable_path(std::string path) {
    std::ranges::replace(path, '\\', '/');
#ifdef _WIN32
    std::ranges::transform(path, path.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif
    return path;
}

[[nodiscard]] std::optional<std::string> cursor_file_path(CXCursor cursor) {
    CXSourceLocation location = clang_getCursorLocation(cursor);
    if (clang_equalLocations(location, clang_getNullLocation())) {
        return std::nullopt;
    }

    CXFile file = nullptr;
    clang_getFileLocation(location, &file, nullptr, nullptr, nullptr);
    if (!file) {
        return std::nullopt;
    }

    const auto filename = clang_string(clang_getFileName(file));
    if (filename.empty()) {
        return std::nullopt;
    }
    return generic_absolute_path(filename);
}

[[nodiscard]] bool
cursor_is_from_header(CXCursor cursor, const TranslationUnitContext& context) {
    const auto file_path = cursor_file_path(cursor);
    if (!file_path) {
        return true;
    }
    return comparable_path(*file_path) == context.comparable_header_path;
}

[[nodiscard]] bool is_class_like(CXCursorKind kind) {
    return kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl;
}

[[nodiscard]] std::string
access_name(CX_CXXAccessSpecifier access, std::string_view fallback) {
    switch (access) {
        case CX_CXXPublic:
            return "public";
        case CX_CXXProtected:
            return "protected";
        case CX_CXXPrivate:
            return "private";
        case CX_CXXInvalidAccessSpecifier:
            break;
    }
    return std::string(fallback);
}

[[nodiscard]] std::string
cursor_access(CXCursor cursor, std::string_view fallback) {
    return access_name(clang_getCXXAccessSpecifier(cursor), fallback);
}

[[nodiscard]] std::string access_from_token_text(
    CXCursor cursor,
    std::string_view fallback,
    CXTranslationUnit translation_unit
) {
    CXToken* tokens = nullptr;
    unsigned token_count = 0;
    clang_tokenize(
        translation_unit,
        clang_getCursorExtent(cursor),
        &tokens,
        &token_count
    );

    std::string token_text;
    for (unsigned i = 0; i < token_count; ++i) {
        token_text +=
            clang_string(clang_getTokenSpelling(translation_unit, tokens[i]));
    }
    clang_disposeTokens(translation_unit, tokens, token_count);

    if (contains(token_text, "public")) {
        return "public";
    }
    if (contains(token_text, "protected")) {
        return "protected";
    }
    if (contains(token_text, "private")) {
        return "private";
    }
    return std::string(fallback);
}

[[nodiscard]] std::string qualified_name(CXCursor cursor) {
    if (clang_Cursor_isNull(cursor) ||
        clang_getCursorKind(cursor) == CXCursor_TranslationUnit) {
        return {};
    }

    const auto parent = clang_getCursorSemanticParent(cursor);
    std::string parent_name;
    if (!clang_Cursor_isNull(parent) &&
        clang_getCursorKind(parent) != CXCursor_TranslationUnit) {
        parent_name = qualified_name(parent);
    }

    const auto current = cursor_spelling(cursor);
    if (current.empty()) {
        return parent_name;
    }
    if (parent_name.empty()) {
        return current;
    }
    return parent_name + "::" + current;
}

[[nodiscard]] bool is_builtin_or_preserved_kind(CXTypeKind kind) {
    switch (kind) {
        case CXType_Void:
        case CXType_Bool:
        case CXType_Char_U:
        case CXType_UChar:
        case CXType_Char16:
        case CXType_Char32:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
        case CXType_UInt128:
        case CXType_Char_S:
        case CXType_SChar:
        case CXType_WChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
        case CXType_Int128:
        case CXType_Float:
        case CXType_Double:
        case CXType_LongDouble:
        case CXType_ConstantArray:
        case CXType_IncompleteArray:
        case CXType_Typedef:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool is_builtin_kind(CXTypeKind kind) {
    switch (kind) {
        case CXType_Void:
        case CXType_Bool:
        case CXType_Char_U:
        case CXType_UChar:
        case CXType_Char16:
        case CXType_Char32:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
        case CXType_UInt128:
        case CXType_Char_S:
        case CXType_SChar:
        case CXType_WChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong:
        case CXType_Int128:
        case CXType_Float:
        case CXType_Double:
        case CXType_LongDouble:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] std::string fully_qualified_type(CXType type);

[[nodiscard]] std::string format_function_pointer_type(CXType function_type) {
    auto return_type = fully_qualified_type(clang_getResultType(function_type));

    std::vector<std::string> param_types;
    const int arg_count = clang_getNumArgTypes(function_type);
    for (int i = 0; i < arg_count; ++i) {
        param_types.push_back(fully_qualified_type(
            clang_getArgType(function_type, static_cast<unsigned>(i))
        ));
    }
    if (clang_isFunctionTypeVariadic(function_type)) {
        param_types.emplace_back("...");
    }

    std::string params;
    for (const auto& param_type : param_types) {
        if (!params.empty()) {
            params += ", ";
        }
        params += param_type;
    }
    return return_type + "(*)(" + params + ")";
}

[[nodiscard]] std::string fully_qualified_type(CXType type) {
    const auto original_spelling = type_spelling(type);
    const auto canonical_type = clang_getCanonicalType(type);
    const auto canonical_spelling = type_spelling(canonical_type);

    if (canonical_type.kind == CXType_Pointer) {
        const auto pointee = clang_getPointeeType(canonical_type);
        if (pointee.kind == CXType_FunctionProto ||
            pointee.kind == CXType_FunctionNoProto) {
            return format_function_pointer_type(pointee);
        }
        return fully_qualified_type(pointee) + "*";
    }

    if (canonical_type.kind == CXType_LValueReference ||
        canonical_type.kind == CXType_RValueReference) {
        const auto pointee = clang_getPointeeType(canonical_type);
        const auto pointee_type = fully_qualified_type(pointee);
        const auto ref_op =
            canonical_type.kind == CXType_LValueReference ? "&" : "&&";

        if ((pointee.kind == CXType_ConstantArray ||
             pointee.kind == CXType_IncompleteArray) &&
            contains(pointee_type, "[") && contains(pointee_type, "]")) {
            const auto array_start = pointee_type.find('[');
            return pointee_type.substr(0, array_start) + "(" + ref_op + ")" +
                   pointee_type.substr(array_start);
        }
        return pointee_type + ref_op;
    }

    if (is_builtin_or_preserved_kind(canonical_type.kind)) {
        return original_spelling;
    }

    if (type.kind == CXType_Typedef && is_builtin_kind(canonical_type.kind)) {
        return original_spelling;
    }

    if (!canonical_spelling.empty() && !original_spelling.empty() &&
        std::ranges::count(canonical_spelling, '<') >
            std::ranges::count(original_spelling, '<') &&
        std::ranges::count(canonical_spelling, '>') >
            std::ranges::count(original_spelling, '>')) {
        return canonical_spelling;
    }

    if (!original_spelling.empty() && contains(original_spelling, "::")) {
        return original_spelling;
    }

    const auto type_decl = clang_getTypeDeclaration(canonical_type);
    if (!clang_Cursor_isNull(type_decl) &&
        clang_getCursorKind(type_decl) != CXCursor_NoDeclFound) {
        const auto qualified = qualified_name(type_decl);
        if (!qualified.empty()) {
            if (contains(original_spelling, "<") &&
                contains(original_spelling, ">")) {
                const auto template_start = original_spelling.find('<');
                const auto base_name = trim(
                    std::string_view(original_spelling)
                        .substr(0, template_start)
                );
                const auto qualified_base_pos = qualified.rfind("::");
                const auto qualified_base =
                    qualified_base_pos == std::string::npos ?
                        qualified :
                        qualified.substr(qualified_base_pos + 2);
                const auto template_part =
                    original_spelling.substr(template_start);

                if (base_name == qualified_base ||
                    ends_with(base_name, qualified_base)) {
                    return qualified + template_part;
                }
                if (contains(original_spelling, "::")) {
                    return original_spelling;
                }
                return qualified + template_part;
            }
            return qualified;
        }
    }

    return original_spelling.empty() ? canonical_spelling : original_spelling;
}

[[nodiscard]] bool is_incomplete_type(CXType type) {
    const auto canonical_type = clang_getCanonicalType(type);
    if (canonical_type.kind == CXType_Pointer ||
        canonical_type.kind == CXType_LValueReference ||
        canonical_type.kind == CXType_RValueReference) {
        return is_incomplete_type(clang_getPointeeType(canonical_type));
    }

    if (canonical_type.kind == CXType_Record ||
        canonical_type.kind == CXType_Enum) {
        const auto type_decl = clang_getTypeDeclaration(canonical_type);
        if (!clang_Cursor_isNull(type_decl) &&
            clang_getCursorKind(type_decl) != CXCursor_NoDeclFound) {
            return !clang_isCursorDefinition(type_decl);
        }
    }

    return false;
}

[[nodiscard]] std::string ref_qualifier(CXCursor cursor) {
    switch (clang_Type_getCXXRefQualifier(clang_getCursorType(cursor))) {
        case CXRefQualifier_LValue:
            return "&";
        case CXRefQualifier_RValue:
            return "&&";
        case CXRefQualifier_None:
            break;
    }

    const auto display_name = cursor_display_name(cursor);
    const auto right_paren = display_name.rfind(')');
    if (right_paren != std::string::npos) {
        auto suffix =
            trim(std::string_view(display_name).substr(right_paren + 1));
        if (starts_with(suffix, "const")) {
            suffix = trim(std::string_view(suffix).substr(5));
        }
        if (starts_with(suffix, "&&")) {
            return "&&";
        }
        if (starts_with(suffix, "&")) {
            return "&";
        }
    }
    return {};
}

[[nodiscard]] bool
has_reflgen_attribute(CXCursor cursor, CXTranslationUnit translation_unit) {
    bool found = false;
    visit_children(cursor, [&](CXCursor child, CXCursor) {
        const auto kind = clang_getCursorKind(child);
        if (kind == CXCursor_AnnotateAttr &&
            trim(cursor_spelling(child)) == "reflgen") {
            found = true;
            return CXChildVisit_Break;
        }

        if (kind == CXCursor_UnexposedAttr) {
            CXToken* tokens = nullptr;
            unsigned token_count = 0;
            clang_tokenize(
                translation_unit,
                clang_getCursorExtent(child),
                &tokens,
                &token_count
            );

            std::string token_text;
            for (unsigned i = 0; i < token_count; ++i) {
                token_text += clang_string(
                    clang_getTokenSpelling(translation_unit, tokens[i])
                );
            }
            clang_disposeTokens(translation_unit, tokens, token_count);

            if (contains(token_text, "reflgen")) {
                found = true;
                return CXChildVisit_Break;
            }
        }
        return CXChildVisit_Continue;
    });
    return found;
}

[[nodiscard]] std::vector<ParamInfo> parameters_for(CXCursor cursor) {
    std::vector<ParamInfo> params;
    visit_children(cursor, [&](CXCursor child, CXCursor) {
        if (clang_getCursorKind(child) == CXCursor_ParmDecl) {
            params.push_back({
                .name = cursor_spelling(child),
                .type_name = fully_qualified_type(clang_getCursorType(child)),
            });
        }
        return CXChildVisit_Continue;
    });
    return params;
}

[[nodiscard]] std::optional<EnumInfo>
parse_enum(CXCursor cursor, const TranslationUnitContext& context) {
    if (!clang_isCursorDefinition(cursor) ||
        !has_reflgen_attribute(cursor, context.translation_unit)) {
        return std::nullopt;
    }

    auto enum_name = cursor_spelling(cursor);
    if (enum_name.empty()) {
        return std::nullopt;
    }

    auto enum_info = EnumInfo {
        .name = qualified_name(cursor),
        .source_file = context.header_path,
        .underlying_type =
            fully_qualified_type(clang_getEnumDeclIntegerType(cursor)),
        .is_scoped = clang_EnumDecl_isScoped(cursor) != 0,
    };
    if (enum_info.name.empty()) {
        enum_info.name = std::move(enum_name);
    }

    visit_children(cursor, [&](CXCursor child, CXCursor) {
        if (clang_getCursorKind(child) == CXCursor_EnumConstantDecl) {
            enum_info.values.push_back({
                .name = cursor_spelling(child),
                .value = clang_getEnumConstantDeclValue(child),
            });
        }
        return CXChildVisit_Continue;
    });

    return enum_info;
}

[[nodiscard]] std::optional<ClassInfo> parse_class(
    CXCursor cursor,
    std::string_view default_access,
    const TranslationUnitContext& context
) {
    if (!clang_isCursorDefinition(cursor) ||
        clang_getCursorKind(cursor) == CXCursor_ClassTemplate) {
        return std::nullopt;
    }

    auto class_name = cursor_spelling(cursor);
    if (class_name.empty() ||
        !has_reflgen_attribute(cursor, context.translation_unit)) {
        return std::nullopt;
    }

    auto qualified_class_name = qualified_name(cursor);
    if (qualified_class_name.empty()) {
        qualified_class_name = class_name;
    }

    const auto display_name = cursor_display_name(cursor);
    if (!display_name.empty() && display_name != class_name &&
        contains(display_name, "<")) {
        const auto scope_pos = qualified_class_name.rfind("::");
        if (scope_pos != std::string::npos) {
            qualified_class_name =
                qualified_class_name.substr(0, scope_pos + 2) + display_name;
        } else {
            qualified_class_name = display_name;
        }
    }

    ClassInfo class_info {
        .name = std::move(qualified_class_name),
        .source_file = context.header_path,
    };

    std::string current_access(default_access);
    visit_children(cursor, [&](CXCursor child, CXCursor) {
        const auto kind = clang_getCursorKind(child);
        if (kind == CXCursor_CXXAccessSpecifier) {
            current_access = access_from_token_text(
                child,
                current_access,
                context.translation_unit
            );
            return CXChildVisit_Continue;
        }

        const auto member_access = cursor_access(child, current_access);
        if (kind == CXCursor_FieldDecl) {
            const auto field_type = clang_getCursorType(child);
            if (!is_incomplete_type(field_type)) {
                class_info.properties.push_back({
                    .name = cursor_spelling(child),
                    .type_name = fully_qualified_type(field_type),
                    .access = member_access,
                });
            }
        } else if (kind == CXCursor_CXXMethod) {
            if (clang_CXXMethod_isDeleted(child)) {
                return CXChildVisit_Continue;
            }

            auto qualifier = ref_qualifier(child);
            if (!qualifier.empty()) {
                return CXChildVisit_Continue;
            }

            MethodInfo method;
            method.name = cursor_spelling(child);
            method.type_name =
                fully_qualified_type(clang_getCursorResultType(child));
            method.access = member_access;
            method.parameters = parameters_for(child);
            method.is_static = clang_CXXMethod_isStatic(child) != 0;
            method.is_const = clang_CXXMethod_isConst(child) != 0;
            method.ref_qualifier = std::move(qualifier);
            method.is_abstract = clang_CXXMethod_isPureVirtual(child) != 0;
            class_info.methods.push_back(std::move(method));
        } else if (kind == CXCursor_Constructor) {
            if (clang_CXXMethod_isDeleted(child)) {
                return CXChildVisit_Continue;
            }

            MethodInfo constructor;
            constructor.name = class_info.name;
            constructor.type_name = class_info.name;
            constructor.access = member_access;
            constructor.parameters = parameters_for(child);
            class_info.constructors.push_back(std::move(constructor));
        }

        return CXChildVisit_Continue;
    });

    return class_info;
}

[[nodiscard]] ParseResult parse_cursor(
    CXCursor cursor,
    const TranslationUnitContext& context,
    std::string_view current_access = "private",
    CXCursorKind parent_kind = CXCursor_NoDeclFound
) {
    ParseResult result;
    if (!cursor_is_from_header(cursor, context)) {
        return result;
    }

    const auto kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl) {
        if (is_class_like(parent_kind) &&
            (current_access == "private" || current_access == "protected")) {
            return result;
        }

        if (auto class_info = parse_class(
                cursor,
                kind == CXCursor_ClassDecl ? "private" : "public",
                context
            )) {
            result.classes.push_back(std::move(*class_info));
        }
    } else if (kind == CXCursor_EnumDecl) {
        if (is_class_like(parent_kind) &&
            (current_access == "private" || current_access == "protected")) {
            return result;
        }

        if (auto enum_info = parse_enum(cursor, context)) {
            result.enums.push_back(std::move(*enum_info));
        }
    }

    if (kind == CXCursor_ClassTemplate) {
        return result;
    }

    if (kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl) {
        std::string child_access =
            kind == CXCursor_ClassDecl ? "private" : "public";
        visit_children(cursor, [&](CXCursor child, CXCursor) {
            if (clang_getCursorKind(child) == CXCursor_CXXAccessSpecifier) {
                child_access = access_from_token_text(
                    child,
                    child_access,
                    context.translation_unit
                );
            } else {
                auto child_result =
                    parse_cursor(child, context, child_access, kind);
                result.classes.insert(
                    result.classes.end(),
                    std::make_move_iterator(child_result.classes.begin()),
                    std::make_move_iterator(child_result.classes.end())
                );
                result.enums.insert(
                    result.enums.end(),
                    std::make_move_iterator(child_result.enums.begin()),
                    std::make_move_iterator(child_result.enums.end())
                );
            }
            return CXChildVisit_Continue;
        });
    } else {
        visit_children(cursor, [&](CXCursor child, CXCursor) {
            auto child_result =
                parse_cursor(child, context, current_access, kind);
            result.classes.insert(
                result.classes.end(),
                std::make_move_iterator(child_result.classes.begin()),
                std::make_move_iterator(child_result.classes.end())
            );
            result.enums.insert(
                result.enums.end(),
                std::make_move_iterator(child_result.enums.begin()),
                std::make_move_iterator(child_result.enums.end())
            );
            return CXChildVisit_Continue;
        });
    }

    return result;
}

[[nodiscard]] ParseResult parse_header(
    const std::string& header,
    const std::vector<std::string>& include_paths,
    bool verbose
) {
    const auto header_path = generic_absolute_path(header);
    if (!std::filesystem::exists(header_path)) {
        std::cerr << "Warning: Header file '" << header << "' not found\n";
        return {};
    }

    CXIndex index = clang_createIndex(0, 0);
    std::vector<std::string> args =
        {"-x", "c++-header", "-std=c++23", "-DFEI_REFLGEN_SCRIPT"};
    for (const auto& include_path : include_paths) {
        args.emplace_back("-I");
        args.push_back(include_path);
    }

    std::vector<const char*> c_args;
    c_args.reserve(args.size());
    for (const auto& arg : args) {
        c_args.push_back(arg.c_str());
    }

    CXTranslationUnit translation_unit = clang_parseTranslationUnit(
        index,
        header_path.c_str(),
        c_args.data(),
        static_cast<int>(c_args.size()),
        nullptr,
        0,
        CXTranslationUnit_None
    );

    if (!translation_unit) {
        clang_disposeIndex(index);
        std::cerr << "Warning: Failed to parse " << header << '\n';
        return {};
    }

    const unsigned diagnostic_count = clang_getNumDiagnostics(translation_unit);
    for (unsigned i = 0; i < diagnostic_count; ++i) {
        CXDiagnostic diagnostic = clang_getDiagnostic(translation_unit, i);
        const auto severity = clang_getDiagnosticSeverity(diagnostic);
        if (severity >= CXDiagnostic_Warning) {
            std::cerr << "Warning in " << header << ": "
                      << clang_string(clang_getDiagnosticSpelling(diagnostic))
                      << '\n';
        } else if (verbose) {
            std::cerr << "Note in " << header << ": "
                      << clang_string(clang_getDiagnosticSpelling(diagnostic))
                      << '\n';
        }
        clang_disposeDiagnostic(diagnostic);
    }

    TranslationUnitContext context {
        .translation_unit = translation_unit,
        .header_path = header_path,
        .comparable_header_path = comparable_path(header_path),
    };

    auto result =
        parse_cursor(clang_getTranslationUnitCursor(translation_unit), context);

    clang_disposeTranslationUnit(translation_unit);
    clang_disposeIndex(index);
    return result;
}

} // namespace

HeaderParser::HeaderParser(
    std::vector<std::string> headers,
    std::vector<std::string> include_paths,
    bool verbose
) :
    headers_(std::move(headers)), include_paths_(std::move(include_paths)),
    verbose_(verbose) {}

ParseResult HeaderParser::parse() {
    ParseResult result;
    for (const auto& header : headers_) {
        auto header_result = parse_header(header, include_paths_, verbose_);
        result.classes.insert(
            result.classes.end(),
            std::make_move_iterator(header_result.classes.begin()),
            std::make_move_iterator(header_result.classes.end())
        );
        result.enums.insert(
            result.enums.end(),
            std::make_move_iterator(header_result.enums.begin()),
            std::make_move_iterator(header_result.enums.end())
        );
    }
    return result;
}

} // namespace fei::reflgen
