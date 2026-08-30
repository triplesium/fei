#include "codegen.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>

namespace ets::reflgen {
namespace {

[[nodiscard]] std::filesystem::path
normalized_absolute_path(const std::filesystem::path& path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        absolute = path;
    }
    return absolute.lexically_normal();
}

[[nodiscard]] std::string include_path_for(
    const std::string& source_file,
    const std::filesystem::path& root_dir
) {
    const auto source = normalized_absolute_path(source_file);
    const auto root = normalized_absolute_path(root_dir);

    std::error_code error;
    auto relative = std::filesystem::relative(source, root, error);
    if (error) {
        relative = source.lexically_relative(root);
    }
    return relative.generic_string();
}

[[nodiscard]] std::vector<ClassInfo>
sorted_classes(const std::vector<ClassInfo>& classes) {
    auto sorted = classes;
    std::ranges::sort(sorted, {}, &ClassInfo::name);
    return sorted;
}

[[nodiscard]] std::vector<AnnotationSchemaInfo>
sorted_annotation_schemas(const std::vector<AnnotationSchemaInfo>& schemas) {
    auto sorted = schemas;
    std::ranges::sort(sorted, {}, &AnnotationSchemaInfo::reflected_name);
    return sorted;
}

[[nodiscard]] std::vector<EnumInfo>
sorted_enums(const std::vector<EnumInfo>& enums) {
    auto sorted = enums;
    std::ranges::sort(sorted, {}, &EnumInfo::name);
    return sorted;
}

[[nodiscard]] std::string
joined_param_types(const std::vector<ParamInfo>& parameters) {
    std::string result;
    for (const auto& param : parameters) {
        if (!result.empty()) {
            result += ", ";
        }
        result += param.type_name;
    }
    return result;
}

void write_structured_type_name(
    std::ostream& out,
    const std::vector<std::string>& namespace_path,
    std::string_view local_name
) {
    out << "({";
    for (std::size_t index = 0; index < namespace_path.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        out << "\"" << namespace_path[index] << "\"";
    }
    out << "}, \"" << local_name << "\")";
}

[[nodiscard]] const ReflectionAnnotation* find_annotation(
    const std::vector<ReflectionAnnotation>& annotations,
    std::string_view name
) {
    const auto found = std::ranges::find_if(
        annotations,
        [name](const ReflectionAnnotation& annotation) {
            return annotation.name == name;
        }
    );
    return found == annotations.end() ? nullptr : &*found;
}

[[nodiscard]] std::optional<std::string> annotation_value(
    const std::vector<ReflectionAnnotation>& annotations,
    std::string_view annotation_name,
    std::string_view field
) {
    const auto* annotation = find_annotation(annotations, annotation_name);
    if (!annotation) {
        return std::nullopt;
    }
    const auto found = std::ranges::find_if(
        annotation->arguments,
        [field](const AnnotationArgument& argument) {
            return argument.name == field;
        }
    );
    if (found == annotation->arguments.end()) {
        return std::nullopt;
    }
    return found->value;
}

void write_annotations(
    std::ostream& out,
    std::string_view type_name,
    const std::vector<ReflectionAnnotation>& annotations
) {
    for (const auto& annotation : annotations) {
        out << "AnnotationWriter::add<" << type_name << ">(registry, \""
            << annotation.name << "\");\n";
        for (const auto& argument : annotation.arguments) {
            out << "AnnotationWriter::add_field<" << type_name
                << ">(registry, \"" << annotation.name << "\", \""
                << argument.name << "\", \"" << argument.value << "\");\n";
        }
    }
}

[[nodiscard]] std::string default_plugin_name(std::string_view type_name) {
    constexpr std::string_view c_root_namespace = "ets::";
    constexpr std::string_view c_plugin_suffix = "Plugin";
    std::string name(
        type_name.starts_with(c_root_namespace) ?
            type_name.substr(c_root_namespace.size()) :
            type_name
    );
    const auto namespace_end = name.rfind("::");
    const auto local_name_start =
        namespace_end == std::string::npos ? 0 : namespace_end + 2;
    if (name.size() - local_name_start > c_plugin_suffix.size() &&
        name.ends_with(c_plugin_suffix)) {
        name.erase(name.size() - c_plugin_suffix.size());
    }
    return name;
}

[[nodiscard]] std::optional<std::string> plugin_name(const ClassInfo& cls) {
    if (!has_annotation(cls.annotations, "Plugin")) {
        return std::nullopt;
    }
    const auto* plugin = find_annotation(cls.annotations, "Plugin");
    for (const auto& argument : plugin->arguments) {
        if (argument.name != "name") {
            throw std::runtime_error(
                "Unknown Plugin annotation field '" + argument.name +
                "' on class " + cls.name
            );
        }
    }
    if (auto explicit_name =
            annotation_value(cls.annotations, "Plugin", "name")) {
        return explicit_name;
    }
    return default_plugin_name(cls.name);
}

} // namespace

void generate_cpp_file(
    const ParseResult& result,
    const std::filesystem::path& root_dir,
    const std::filesystem::path& output_file,
    const std::string& function_name,
    const std::string& script_module
) {
    if (output_file.has_parent_path()) {
        std::filesystem::create_directories(output_file.parent_path());
    }

    std::ofstream out(output_file, std::ios::binary);
    if (!out) {
        throw std::runtime_error(
            "failed to open output file: " + output_file.string()
        );
    }

    out << "// This file is generated by entisium-reflgen\n\n";
    out << "#include \"refl/registry.hpp\"\n";
    out << "#include \"refl/cls.hpp\"\n";
    out << "#include \"refl/enum.hpp\"\n";
    if (std::ranges::any_of(result.classes, [](const ClassInfo& cls) {
            return has_annotation(cls.annotations, "Plugin");
        })) {
        out << "#include \"app/plugin_registry.hpp\"\n";
    }
    out << "\n";
    out << "#include <cstdint>\n\n";

    std::set<std::string> includes;
    for (const auto& cls : result.classes) {
        includes.insert(include_path_for(cls.source_file, root_dir));
    }
    for (const auto& schema : result.annotation_schemas) {
        includes.insert(include_path_for(schema.source_file, root_dir));
    }
    for (const auto& enum_info : result.enums) {
        includes.insert(include_path_for(enum_info.source_file, root_dir));
    }
    for (const auto& include : includes) {
        out << "#include \"" << include << "\"\n";
    }

    out << "\nnamespace ets::refl::generated {\n";
    out << "void " << function_name << "(Registry& registry) {\n\n";

    for (const auto& schema :
         sorted_annotation_schemas(result.annotation_schemas)) {
        out << "registry.register_cls<" << schema.type_name << ">()\n";
        for (const auto& field : schema.fields) {
            if (field.access != "public") {
                throw std::runtime_error(
                    "Annotation schema field '" + schema.type_name + "." +
                    field.name + "' must be public"
                );
            }
            out << "    .add_property(\"" << field.name << "\", &"
                << schema.type_name << "::" << field.name << ")\n";
        }
        out << "    ;\n";
        out << "AnnotationWriter::register_schema<" << schema.type_name
            << ">(\n";
        out << "    registry,\n";
        out << "    \"" << schema.reflected_name << "\",\n";
        out << "    {";
        for (std::size_t index = 0; index < schema.fields.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            out << "\"" << schema.fields[index].name << "\"";
        }
        out << "},\n";
        out << "    [](AnnotationView annotation) {\n";
        out << "        " << schema.type_name << " value {};\n";
        for (const auto& field : schema.fields) {
            out << "        if (const auto field = annotation.value(\""
                << field.name << "\")) {\n";
            out << "            value." << field.name
                << " = parse_generated_annotation_value<decltype(value."
                << field.name << ")>(*field);\n";
            out << "        }\n";
        }
        out << "        return value;\n";
        out << "    },\n";
        out << "    [](const " << schema.type_name << "& value) {\n";
        out << "        return std::vector<AnnotationField> {\n";
        for (const auto& field : schema.fields) {
            out << "            {\"" << field.name
                << "\", format_generated_annotation_value(value." << field.name
                << ")},\n";
        }
        out << "        };\n";
        out << "    }\n";
        out << ");\n";
    }

    if (!result.annotation_schemas.empty()) {
        out << "\n";
    }

    for (const auto& cls : sorted_classes(result.classes)) {
        if (!is_reflected_class(cls)) {
            continue;
        }
        const auto generated_plugin_name = plugin_name(cls);

        out << "registry.register_cls<" << cls.name << ">";
        write_structured_type_name(out, cls.namespace_path, cls.local_name);
        out << "\n";
        for (const auto& prop : cls.properties) {
            if (is_reflected_property(prop)) {
                out << "    .add_property(\"" << prop.name << "\", &"
                    << cls.name << "::" << prop.name << ")\n";
            }
        }
        for (const auto& method : cls.methods) {
            if (is_reflected_method(cls, method)) {
                out << "    .add_method(\"" << method.name << "\", static_cast<"
                    << method.to_cpp_type(cls.name) << ">(&" << cls.name
                    << "::" << method.name << "))\n";
            }
        }
        for (const auto& constructor : cls.constructors) {
            if (!is_reflected_constructor(cls, constructor)) {
                continue;
            }
            if (constructor.parameters.empty()) {
                out << "    .add_constructor<" << cls.name << ">()\n";
            } else {
                out << "    .add_constructor<" << cls.name << ", "
                    << joined_param_types(constructor.parameters) << ">()\n";
            }
        }
        out << "    ;\n";
        write_annotations(out, cls.name, cls.annotations);
        if (!script_module.empty()) {
            out << "AnnotationWriter::add_field<" << cls.name
                << R"(>(registry, "ScriptModule", "name", ")" << script_module
                << "\");\n";
        }
        if (generated_plugin_name) {
            out << "AnnotationWriter::add_field<" << cls.name
                << R"(>(registry, "Plugin", "name", ")"
                << *generated_plugin_name << "\");\n";
            out << "register_generated_plugin<" << cls.name << ">(\""
                << *generated_plugin_name << "\");\n";
        }
    }

    out << "\n";

    for (const auto& enum_info : sorted_enums(result.enums)) {
        if (has_annotation(enum_info.annotations, "Plugin")) {
            throw std::runtime_error(
                "Plugin annotation can only be applied to classes: " +
                enum_info.name
            );
        }
        out << "registry.register_enum<" << enum_info.name << ">";
        write_structured_type_name(
            out,
            enum_info.namespace_path,
            enum_info.local_name
        );
        out << "\n";
        for (const auto& enum_value : enum_info.values) {
            out << "    .add_enumerator(\"" << enum_value.name
                << "\", static_cast<std::int64_t>(" << enum_info.name
                << "::" << enum_value.name << "))\n";
        }
        out << "    ;\n";
        write_annotations(out, enum_info.name, enum_info.annotations);
        if (!script_module.empty()) {
            out << "AnnotationWriter::add_field<" << enum_info.name
                << R"(>(registry, "ScriptModule", "name", ")" << script_module
                << "\");\n";
        }
    }

    out << "\n}\n";
    out << "} // namespace ets::refl::generated\n";
}

void generate_aggregate_cpp_file(
    const std::vector<std::string>& function_names,
    const std::filesystem::path& output_file
) {
    if (output_file.has_parent_path()) {
        std::filesystem::create_directories(output_file.parent_path());
    }

    std::ofstream out(output_file, std::ios::binary);
    if (!out) {
        throw std::runtime_error(
            "failed to open output file: " + output_file.string()
        );
    }

    out << "// This file is generated by entisium-reflgen\n\n";
    out << "#include \"refl/generated.hpp\"\n";
    out << "#include \"refl/registry.hpp\"\n\n";

    out << "namespace ets::refl::generated {\n";
    for (const auto& function_name : function_names) {
        out << "void " << function_name << "(Registry& registry);\n";
    }
    out << "} // namespace ets::refl::generated\n\n";

    out << "namespace ets {\n\n";
    out << "void register_generated_reflection() {\n";
    out << "    auto& registry = Registry::instance();\n";
    for (const auto& function_name : function_names) {
        out << "    refl::generated::" << function_name << "(registry);\n";
    }
    out << "    refl::generated::AnnotationWriter::validate(registry);\n";
    out << "}\n\n";
    out << "} // namespace ets\n";
}

} // namespace ets::reflgen
