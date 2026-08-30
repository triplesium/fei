#include "manifest.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ets::luau_defgen {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::vector<Annotation> annotations_from(const Json& value) {
    std::vector<Annotation> result;
    for (const auto& annotation : value) {
        result.push_back(
            Annotation {.name = annotation.at("name").get<std::string>()}
        );
    }
    return result;
}

[[nodiscard]] std::vector<Parameter> parameters_from(const Json& value) {
    std::vector<Parameter> result;
    for (const auto& parameter : value) {
        result.push_back(
            Parameter {
                .name = parameter.at("name").get<std::string>(),
                .cpp_type = parameter.at("cppType").get<std::string>(),
            }
        );
    }
    return result;
}

[[nodiscard]] Class class_from(const Json& value, const std::string& module) {
    Class result {
        .cpp_name = value.at("cppName").get<std::string>(),
        .name = value.at("name").get<std::string>(),
        .namespace_path = value.at("namespace").get<std::vector<std::string>>(),
        .source = value.at("source").get<std::string>(),
        .module = module,
        .abstract = value.at("abstract").get<bool>(),
        .annotations = annotations_from(value.at("annotations")),
    };
    for (const auto& property : value.at("properties")) {
        result.properties.push_back(
            Property {
                .name = property.at("name").get<std::string>(),
                .cpp_type = property.at("cppType").get<std::string>(),
            }
        );
    }
    for (const auto& method : value.at("methods")) {
        result.methods.push_back(
            Method {
                .name = method.at("name").get<std::string>(),
                .return_cpp_type =
                    method.at("returnCppType").get<std::string>(),
                .parameters = parameters_from(method.at("parameters")),
                .is_static = method.at("static").get<bool>(),
                .is_const = method.at("const").get<bool>(),
            }
        );
    }
    for (const auto& constructor : value.at("constructors")) {
        result.constructors.push_back(
            Constructor {
                .parameters = parameters_from(constructor.at("parameters")),
            }
        );
    }
    return result;
}

[[nodiscard]] Enum enum_from(const Json& value, const std::string& module) {
    Enum result {
        .cpp_name = value.at("cppName").get<std::string>(),
        .name = value.at("name").get<std::string>(),
        .namespace_path = value.at("namespace").get<std::vector<std::string>>(),
        .source = value.at("source").get<std::string>(),
        .module = module,
        .annotations = annotations_from(value.at("annotations")),
    };
    for (const auto& enumerator : value.at("values")) {
        result.values.push_back(
            Enumerator {
                .name = enumerator.at("name").get<std::string>(),
                .value = enumerator.at("value").dump(),
            }
        );
    }
    return result;
}

[[nodiscard]] Json read_manifest(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "Failed to open reflection manifest '" + path.string() + "'"
        );
    }
    try {
        return Json::parse(input);
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "Failed to parse reflection manifest '" + path.string() +
            "': " + error.what()
        );
    }
}

} // namespace

Database
load_manifests(const std::span<const std::filesystem::path> manifest_files) {
    Database result;
    std::unordered_map<std::string, std::filesystem::path> cpp_names;
    std::unordered_map<std::string, std::string> luau_names;

    for (const auto& path : manifest_files) {
        const auto document = read_manifest(path);
        if (document.value("format", "") != "entisium.reflection" ||
            document.value("version", 0) != 1) {
            throw std::runtime_error(
                "Unsupported reflection manifest format in '" + path.string() +
                "'"
            );
        }
        const auto module = document.at("module").get<std::string>();
        const auto remember_type = [&](const std::string& cpp_name,
                                       const std::string& name) {
            if (const auto existing = cpp_names.find(cpp_name);
                existing != cpp_names.end()) {
                throw std::runtime_error(
                    "Reflected C++ type '" + cpp_name + "' occurs in both '" +
                    existing->second.string() + "' and '" + path.string() + "'"
                );
            }
            if (const auto existing = luau_names.find(name);
                existing != luau_names.end() && existing->second != cpp_name) {
                std::string message = "Luau type name '";
                message.append(name)
                    .append("' is ambiguous between '")
                    .append(existing->second)
                    .append("' and '")
                    .append(cpp_name)
                    .append("'");
                throw std::runtime_error(message);
            }
            cpp_names.emplace(cpp_name, path);
            luau_names.emplace(name, cpp_name);
        };

        for (const auto& value : document.at("classes")) {
            auto parsed = class_from(value, module);
            remember_type(parsed.cpp_name, parsed.name);
            result.classes.push_back(std::move(parsed));
        }
        for (const auto& value : document.at("enums")) {
            auto parsed = enum_from(value, module);
            remember_type(parsed.cpp_name, parsed.name);
            result.enums.push_back(std::move(parsed));
        }
    }

    std::ranges::sort(result.classes, {}, &Class::cpp_name);
    std::ranges::sort(result.enums, {}, &Enum::cpp_name);
    return result;
}

} // namespace ets::luau_defgen
