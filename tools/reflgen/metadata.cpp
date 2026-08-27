#include "metadata.hpp"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace ets::reflgen {
namespace {

struct MetadataField {
    std::string name;
    std::string value;
};

struct MetadataSchema {
    std::string name;
    std::string type;
    std::string source;
    std::map<std::string, std::string> fields;
};

struct MetadataUse {
    std::string name;
    std::string type;
    std::string source;
    std::vector<MetadataField> fields;
};

[[nodiscard]] std::string field_kind(std::string_view type) {
    if (type == "bool") {
        return "bool";
    }
    if (type == "char" || type == "signed char" || type == "short" ||
        type == "int" || type == "long" || type == "long long" ||
        type == "std::int8_t" || type == "std::int16_t" ||
        type == "std::int32_t" || type == "std::int64_t") {
        return "signed-integer";
    }
    if (type == "unsigned char" || type == "unsigned short" ||
        type == "unsigned int" || type == "unsigned long" ||
        type == "unsigned long long" || type == "std::uint8_t" ||
        type == "std::uint16_t" || type == "std::uint32_t" ||
        type == "std::uint64_t") {
        return "unsigned-integer";
    }
    if (type == "float" || type == "double" || type == "long double") {
        return "floating";
    }
    if (type == "std::string" || type.contains("basic_string<char")) {
        return "string";
    }
    return "unsupported:" + std::string(type);
}

void write_use(
    std::ostream& out,
    const ReflectionAnnotation& annotation,
    std::string_view type,
    std::string_view source
) {
    out << "use " << std::quoted(annotation.name) << ' ' << std::quoted(type)
        << ' ' << std::quoted(source) << ' ' << annotation.arguments.size();
    for (const auto& argument : annotation.arguments) {
        out << ' ' << std::quoted(argument.name) << ' '
            << std::quoted(argument.value);
    }
    out << '\n';
}

void require_read(bool success, const std::filesystem::path& file) {
    if (!success) {
        throw std::runtime_error(
            "Malformed reflection metadata file '" + file.generic_string() + "'"
        );
    }
}

void validate_value(
    std::string_view kind,
    std::string_view value,
    const MetadataUse& use,
    std::string_view field
) {
    bool valid = true;
    if (kind == "bool") {
        valid = value == "true" || value == "false";
    } else if (kind == "signed-integer") {
        long long parsed = 0;
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), parsed);
        valid = error == std::errc {} && end == value.data() + value.size();
    } else if (kind == "unsigned-integer") {
        unsigned long long parsed = 0;
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), parsed);
        valid = error == std::errc {} && end == value.data() + value.size();
    } else if (kind == "floating") {
        long double parsed = 0;
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), parsed);
        valid = error == std::errc {} && end == value.data() + value.size();
    }
    if (!valid) {
        throw std::runtime_error(
            "Invalid " + std::string(kind) + " value '" + std::string(value) +
            "' for annotation '" + use.name + "." + std::string(field) +
            "' on type '" + use.type + "' in " + use.source
        );
    }
}

} // namespace

void write_reflection_metadata(
    const ParseResult& result,
    const std::filesystem::path& output_file,
    const std::string& script_module
) {
    if (output_file.empty()) {
        return;
    }
    if (output_file.has_parent_path()) {
        std::filesystem::create_directories(output_file.parent_path());
    }
    std::ofstream out(output_file, std::ios::binary);
    if (!out) {
        throw std::runtime_error(
            "Failed to open reflection metadata file '" +
            output_file.generic_string() + "'"
        );
    }

    for (const auto& schema : result.annotation_schemas) {
        out << "schema " << std::quoted(schema.reflected_name) << ' '
            << std::quoted(schema.type_name) << ' '
            << std::quoted(schema.source_file) << ' ' << schema.fields.size();
        for (const auto& field : schema.fields) {
            out << ' ' << std::quoted(field.name) << ' '
                << std::quoted(field_kind(field.type_name));
        }
        out << '\n';
    }

    const auto write_type = [&](const auto& type) {
        for (const auto& annotation : type.annotations) {
            write_use(out, annotation, type.name, type.source_file);
        }
        if (!script_module.empty()) {
            write_use(
                out,
                ReflectionAnnotation {
                    .name = "ScriptModule",
                    .arguments = {{.name = "name", .value = script_module}},
                },
                type.name,
                type.source_file
            );
        }
    };
    for (const auto& cls : result.classes) {
        write_type(cls);
    }
    for (const auto& enm : result.enums) {
        write_type(enm);
    }
}

void validate_reflection_metadata(
    const std::vector<std::string>& metadata_files
) {
    std::map<std::string, MetadataSchema> schemas;
    std::map<std::string, std::string> schema_names_by_type;
    std::vector<MetadataUse> uses;

    const auto add_builtin = [&](MetadataSchema schema) {
        schema_names_by_type.emplace(schema.type, schema.name);
        schemas.emplace(schema.name, std::move(schema));
    };
    add_builtin({
        .name = "ScriptPrelude",
        .type = "ets::annotations::ScriptPrelude",
        .source = "refl/annotations.hpp",
    });
    add_builtin({
        .name = "ScriptModule",
        .type = "ets::annotations::ScriptModule",
        .source = "refl/annotations.hpp",
        .fields = {{"name", "string"}},
    });

    for (const auto& filename : metadata_files) {
        const std::filesystem::path file {filename};
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            throw std::runtime_error(
                "Missing reflection metadata file '" + file.generic_string() +
                "'"
            );
        }
        std::string record;
        while (in >> record) {
            std::string name;
            std::string type;
            std::string source;
            std::size_t field_count = 0;
            require_read(
                static_cast<bool>(
                    in >> std::quoted(name) >> std::quoted(type) >>
                    std::quoted(source) >> field_count
                ),
                file
            );
            if (record == "schema") {
                MetadataSchema schema {
                    .name = std::move(name),
                    .type = std::move(type),
                    .source = std::move(source),
                };
                for (std::size_t index = 0; index < field_count; ++index) {
                    std::string field;
                    std::string kind;
                    require_read(
                        static_cast<bool>(
                            in >> std::quoted(field) >> std::quoted(kind)
                        ),
                        file
                    );
                    schema.fields.emplace(std::move(field), std::move(kind));
                }
                const auto named = schemas.find(schema.name);
                if (named != schemas.end() &&
                    (named->second.type != schema.type ||
                     named->second.fields != schema.fields)) {
                    throw std::runtime_error(
                        "Annotation schema '" + schema.name +
                        "' has conflicting declarations in " +
                        named->second.source + " and " + schema.source
                    );
                }
                const auto typed = schema_names_by_type.find(schema.type);
                if (typed != schema_names_by_type.end() &&
                    typed->second != schema.name) {
                    throw std::runtime_error(
                        "Annotation schema type '" + schema.type +
                        "' is registered as both '" + typed->second +
                        "' and '" + schema.name + "'"
                    );
                }
                schema_names_by_type.emplace(schema.type, schema.name);
                schemas.emplace(schema.name, std::move(schema));
            } else if (record == "use") {
                MetadataUse use {
                    .name = std::move(name),
                    .type = std::move(type),
                    .source = std::move(source),
                };
                for (std::size_t index = 0; index < field_count; ++index) {
                    MetadataField field;
                    require_read(
                        static_cast<bool>(
                            in >> std::quoted(field.name) >>
                            std::quoted(field.value)
                        ),
                        file
                    );
                    use.fields.push_back(std::move(field));
                }
                uses.push_back(std::move(use));
            } else {
                throw std::runtime_error(
                    "Unknown reflection metadata record '" + record + "' in " +
                    file.generic_string()
                );
            }
        }
    }

    for (const auto& [name, schema] : schemas) {
        for (const auto& [field, kind] : schema.fields) {
            if (kind.starts_with("unsupported:")) {
                std::string message = "Unsupported field type '";
                message.append(
                    kind.substr(std::string_view("unsupported:").size())
                );
                message.append("' for annotation schema '")
                    .append(name)
                    .append(".")
                    .append(field)
                    .append("' in ")
                    .append(schema.source);
                throw std::runtime_error(message);
            }
        }
    }

    for (const auto& use : uses) {
        const auto schema = schemas.find(use.name);
        if (schema == schemas.end()) {
            throw std::runtime_error(
                "Unknown annotation '" + use.name + "' on type '" + use.type +
                "' in " + use.source
            );
        }
        std::set<std::string> seen_fields;
        for (const auto& field : use.fields) {
            if (!seen_fields.insert(field.name).second) {
                throw std::runtime_error(
                    "Duplicate field '" + field.name + "' for annotation '" +
                    use.name + "' on type '" + use.type + "' in " + use.source
                );
            }
            const auto declared = schema->second.fields.find(field.name);
            if (declared == schema->second.fields.end()) {
                throw std::runtime_error(
                    "Unknown field '" + field.name + "' for annotation '" +
                    use.name + "' on type '" + use.type + "' in " + use.source
                );
            }
            validate_value(declared->second, field.value, use, field.name);
        }
    }
}

} // namespace ets::reflgen
