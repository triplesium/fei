#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/dynamic/access.hpp"
#include "ecs/dynamic/system_param.hpp"
#include "ecs/fwd.hpp"
#include "refl/type.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei {

struct DynamicTypeRef {
    std::string type_name;
    Optional<TypeId> type_id;
};

enum class DynamicQueryFieldDeclKind {
    Component,
    Entity,
};

struct DynamicQueryFieldDecl {
    std::string name;
    DynamicTypeRef type;
    DynamicQueryFieldDeclKind kind {DynamicQueryFieldDeclKind::Component};
    DynamicParamAccess access {DynamicParamAccess::Read};
};

struct DynamicQueryFilterDecl {
    DynamicTypeRef type;
    bool required {true};
};

class DynamicSystemParamDecl {
  public:
    std::string name;

    virtual ~DynamicSystemParamDecl() = default;
    virtual TypeId decl_type_id() const = 0;
    virtual std::string_view decl_type_name() const = 0;
};

using DynamicSystemParamDeclPtr = std::unique_ptr<DynamicSystemParamDecl>;

template<typename T>
class DynamicSystemParamDeclBase : public DynamicSystemParamDecl {
  private:
    friend T;
    DynamicSystemParamDeclBase() = default;

  public:
    TypeId decl_type_id() const override { return type_id<T>(); }
    std::string_view decl_type_name() const override { return type_name<T>(); }
};

struct DynamicResourceParamDecl final
    : DynamicSystemParamDeclBase<DynamicResourceParamDecl> {
    DynamicTypeRef type;
    DynamicParamAccess access {DynamicParamAccess::Read};
    bool optional {false};
};

struct DynamicQueryParamDecl final
    : DynamicSystemParamDeclBase<DynamicQueryParamDecl> {
    std::vector<DynamicQueryFieldDecl> fields;
    std::vector<DynamicQueryFilterDecl> filters;
};

struct DynamicCommandsParamDecl final
    : DynamicSystemParamDeclBase<DynamicCommandsParamDecl> {};

struct DynamicWorldParamDecl final
    : DynamicSystemParamDeclBase<DynamicWorldParamDecl> {};

struct DynamicSystemDecl {
    std::string name;
    std::vector<DynamicSystemParamDeclPtr> params;
    ScheduleId schedule {};
};

class DynamicSystemParamCompilerRegistry {
  public:
    using Compiler =
        std::function<Result<DynamicSystemParamPtr, DynamicSystemError>(
            const DynamicSystemParamDecl&
        )>;

    DynamicSystemParamCompilerRegistry() = default;

    static DynamicSystemParamCompilerRegistry& instance();

    template<typename Decl, typename CompilerFunc>
    void add(CompilerFunc compiler) {
        m_compilers[type_id<Decl>()] =
            [compiler = std::move(compiler)](
                const DynamicSystemParamDecl& decl
            ) -> Result<DynamicSystemParamPtr, DynamicSystemError> {
            return compiler(static_cast<const Decl&>(decl));
        };
    }

    Result<DynamicSystemParamPtr, DynamicSystemError>
    compile(const DynamicSystemParamDecl& decl) const;

  private:
    std::unordered_map<TypeId, Compiler> m_compilers;
};

Result<TypeId, DynamicSystemError>
resolve_dynamic_type_ref(const DynamicTypeRef& type_ref);

Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_system_param(const DynamicSystemParamDecl& param);

Result<DynamicSystemParams, DynamicSystemError>
compile_dynamic_system_params(const DynamicSystemDecl& decl);

} // namespace fei
