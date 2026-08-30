#include "base/optional.hpp"
#include "ecs/dynamic/system_decl.hpp"
#include "ecs/fwd.hpp"
#include "pass.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ets::detail::luau_compiler {
namespace {

using Luau::AstExpr;
using Luau::AstExprCall;
using Luau::AstExprFunction;
using Luau::AstExprGlobal;
using Luau::AstExprIndexName;
using Luau::AstExprLocal;
using Luau::AstLocal;
using Luau::AstStatForIn;
using Luau::AstStatLocal;
using Luau::AstStatLocalFunction;

std::string_view name_view(Luau::AstName name) {
    return name.value != nullptr ? std::string_view {name.value} :
                                   std::string_view {};
}

struct KnownReflectedLocal {
    TypeId type;
    bool writable {false};
};

struct ResolvedPropertyChain {
    const AstExprLocal* root {nullptr};
    KnownReflectedLocal root_value;
    TypeId leaf_type;
    std::vector<std::string> properties;
    std::vector<std::string> local_properties;
    const AstLocal* scalar_alias {nullptr};
};

struct ReusableQueryLoop {
    AstStatForIn* statement {nullptr};
    std::size_t query_field_count {0};
    std::vector<std::pair<const AstLocal*, std::size_t>> fields;
    bool chunk_safe {true};
};

struct ScalarizedPropertyAlias {
    const AstLocal* root {nullptr};
    KnownReflectedLocal root_value;
    TypeId value_type;
    std::vector<std::string> properties;
    Luau::Location initializer;
    bool safe {true};
};

struct PendingAliasPropertyAccess {
    Luau::Location location;
    const AstLocal* alias {nullptr};
    TypeId leaf_type;
    std::vector<std::string> properties;
    std::vector<std::string> local_properties;
};

bool is_direct_luau_leaf(TypeId type) {
    return type == type_id<Entity>() || type == type_id<bool>() ||
           type == type_id<float>() || type == type_id<double>() ||
           type == type_id<signed char>() || type == type_id<unsigned char>() ||
           type == type_id<short>() || type == type_id<unsigned short>() ||
           type == type_id<int>() || type == type_id<unsigned int>() ||
           type == type_id<long>() || type == type_id<unsigned long>() ||
           type == type_id<long long>() ||
           type == type_id<unsigned long long>() ||
           type == type_id<std::string>();
}

class BreakFinder final : public Luau::AstVisitor {
  public:
    bool found {false};

    bool visit(Luau::AstStatBreak* /*statement*/) override {
        found = true;
        return false;
    }

    bool visit(AstExprFunction* /*expression*/) override { return false; }
};

Optional<KnownReflectedLocal>
known_reflected_value(const DynamicTypeRef& type, DynamicParamAccess access) {
    auto resolved = resolve_dynamic_type_ref(type);
    if (!resolved || !Registry::instance().try_get_cls(*resolved)) {
        return nullopt;
    }
    return KnownReflectedLocal {
        .type = *resolved,
        .writable = access == DynamicParamAccess::Write,
    };
}

class BorrowAnalysis final : public Luau::AstVisitor {
  public:
    BorrowAnalysis(
        const AstExprFunction& function,
        const std::vector<DynamicSystemParamDeclPtr>& params,
        std::vector<LuauPropertyPathDecl>& paths,
        SourcePatchSet& patches,
        std::vector<LuauOptimizationPassReport>& reports,
        LuauOptimizationPasses optimization_passes
    ) :
        m_paths(paths), m_patches(patches), m_reports(reports),
        m_optimization_passes(optimization_passes) {
        const std::size_t count = std::min(function.args.size, params.size());
        for (std::size_t index = 0; index < count; ++index) {
            const AstLocal* argument = function.args.data[index];
            const auto& param = *params[index];
            if (param.decl_type_id() == type_id<DynamicResourceParamDecl>()) {
                const auto& resource =
                    static_cast<const DynamicResourceParamDecl&>(param);
                if (auto value =
                        known_reflected_value(resource.type, resource.access)) {
                    m_known.emplace(argument, *value);
                }
                continue;
            }
            if (param.decl_type_id() != type_id<DynamicQueryParamDecl>()) {
                continue;
            }
            const auto& query =
                static_cast<const DynamicQueryParamDecl&>(param);
            std::vector<Optional<KnownReflectedLocal>> fields;
            fields.reserve(query.fields.size());
            for (const auto& field : query.fields) {
                if (field.kind == DynamicQueryFieldDeclKind::Entity) {
                    fields.emplace_back(nullopt);
                } else {
                    fields.push_back(
                        known_reflected_value(field.type, field.access)
                    );
                }
            }
            m_queries.emplace(argument, std::move(fields));
        }
    }

    bool visit(AstStatForIn* statement) override {
        if (statement->values.size != 1) {
            return true;
        }
        const auto* source = statement->values.data[0]->as<AstExprLocal>();
        const auto query =
            source != nullptr ? m_queries.find(source->local) : m_queries.end();
        if (query == m_queries.end()) {
            return true;
        }
        const std::size_t count =
            std::min(statement->vars.size, query->second.size());
        ReusableQueryLoop reusable {
            .statement = statement,
            .query_field_count = query->second.size(),
        };
        BreakFinder breaks;
        statement->body->visit(&breaks);
        reusable.chunk_safe = !breaks.found;
        for (std::size_t index = 0; index < count; ++index) {
            if (query->second[index]) {
                m_known[statement->vars.data[index]] = *query->second[index];
                if (index < 32) {
                    m_reusable.emplace(statement->vars.data[index], true);
                    reusable.fields.emplace_back(
                        statement->vars.data[index],
                        index
                    );
                }
            }
        }
        if (!reusable.fields.empty()) {
            m_reusable_loops.push_back(std::move(reusable));
        }
        return true;
    }

    bool visit(AstExprLocal* expression) override {
        if (const auto reusable = m_reusable.find(expression->local);
            reusable != m_reusable.end()) {
            reusable->second = false;
        }
        if (const auto alias = m_scalar_aliases.find(expression->local);
            alias != m_scalar_aliases.end()) {
            alias->second.safe = false;
        }
        return true;
    }

    bool visit(AstStatLocal* statement) override {
        const std::size_t count =
            std::min(statement->vars.size, statement->values.size);
        for (std::size_t index = 0; index < count; ++index) {
            const AstExpr& value = *statement->values.data[index];
            if (const auto* local = value.as<AstExprLocal>()) {
                if (const auto known = m_known.find(local->local);
                    known != m_known.end()) {
                    m_known[statement->vars.data[index]] = known->second;
                }
                continue;
            }
            if (auto constructed = constructed_value(value)) {
                m_known[statement->vars.data[index]] = *constructed;
                continue;
            }
            auto chain = resolve_chain(value);
            if (chain) {
                m_known[statement->vars.data[index]] = KnownReflectedLocal {
                    .type = chain->leaf_type,
                    .writable = chain->root_value.writable,
                };
                if (!is_direct_luau_leaf(chain->leaf_type) &&
                    Registry::instance().try_get_cls(chain->leaf_type)) {
                    const AstLocal* root = chain->root->local;
                    if (chain->scalar_alias != nullptr) {
                        root = m_scalar_aliases.at(chain->scalar_alias).root;
                    }
                    if (m_reusable.contains(root)) {
                        const AstLocal* alias = statement->vars.data[index];
                        m_scalar_aliases.emplace(
                            alias,
                            ScalarizedPropertyAlias {
                                .root = root,
                                .root_value = chain->root_value,
                                .value_type = chain->leaf_type,
                                .properties = chain->properties,
                                .initializer = value.location,
                            }
                        );
                        m_scalar_alias_order.push_back(alias);
                        m_scalar_alias_initializers.insert(
                            value.as<AstExprIndexName>()
                        );
                        ++report(LuauOptimizationPass::ElidePropertyAliases)
                              .candidates;
                    }
                }
            }
        }
        return true;
    }

    bool visit(AstExprIndexName* expression) override {
        if (m_scalar_alias_initializers.contains(expression)) {
            return false;
        }
        auto chain = resolve_chain(*expression);
        if (!chain || !is_direct_luau_leaf(chain->leaf_type)) {
            return true;
        }
        ++report(LuauOptimizationPass::FlattenPropertyPaths).candidates;
        const AstLocal* reusable_root = chain->root->local;
        if (chain->scalar_alias != nullptr) {
            reusable_root = m_scalar_aliases.at(chain->scalar_alias).root;
        }
        if (m_reusable.contains(reusable_root)) {
            ++m_reusable_property_accesses[reusable_root];
        }
        if (chain->scalar_alias != nullptr) {
            if (chain->root->upvalue) {
                m_scalar_aliases.at(chain->scalar_alias).safe = false;
            }
            m_pending_alias_accesses.push_back(
                PendingAliasPropertyAccess {
                    .location = expression->location,
                    .alias = chain->scalar_alias,
                    .leaf_type = chain->leaf_type,
                    .properties = std::move(chain->properties),
                    .local_properties = std::move(chain->local_properties),
                }
            );
            return false;
        }
        if (chain->root->upvalue) {
            if (const auto reusable = m_reusable.find(chain->root->local);
                reusable != m_reusable.end()) {
                reusable->second = false;
            }
        }
        if (!enabled(LuauOptimizationPass::FlattenPropertyPaths)) {
            return false;
        }
        return add_property_patch(
            expression->location,
            chain->root->local,
            chain->root_value.type,
            chain->leaf_type,
            chain->properties
        );
    }

    void finish() {
        const bool elide_aliases =
            enabled(LuauOptimizationPass::ElidePropertyAliases);
        for (const AstLocal* local : m_scalar_alias_order) {
            auto& alias = m_scalar_aliases.at(local);
            std::string replacement {name_view(alias.root->name)};
            if (elide_aliases && alias.safe) {
                ++report(LuauOptimizationPass::ElidePropertyAliases).applied;
                m_patches.add(
                    alias.initializer,
                    std::move(replacement),
                    luau_optimization_pass_name(
                        LuauOptimizationPass::ElidePropertyAliases
                    )
                );
                continue;
            }
            if (elide_aliases) {
                if (const auto reusable = m_reusable.find(alias.root);
                    reusable != m_reusable.end()) {
                    reusable->second = false;
                }
                for (const auto& property : alias.properties) {
                    replacement.push_back('.');
                    replacement.append(property);
                }
                m_patches.add(
                    alias.initializer,
                    std::move(replacement),
                    luau_optimization_pass_name(
                        LuauOptimizationPass::ElidePropertyAliases
                    )
                );
            }
        }
        for (const auto& access : m_pending_alias_accesses) {
            const auto& alias = m_scalar_aliases.at(access.alias);
            const bool elided = elide_aliases && alias.safe;
            if (enabled(LuauOptimizationPass::FlattenPropertyPaths)) {
                add_property_patch(
                    access.location,
                    access.alias,
                    elided ? alias.root_value.type : alias.value_type,
                    access.leaf_type,
                    elided ? access.properties : access.local_properties
                );
            } else if (elided) {
                add_generic_property_patch(
                    access.location,
                    access.alias,
                    access.properties,
                    LuauOptimizationPass::ElidePropertyAliases
                );
            }
        }
        for (const auto& loop : m_reusable_loops) {
            std::uint32_t reusable_mask = 0;
            for (const auto& [local, index] : loop.fields) {
                if (m_reusable.at(local)) {
                    reusable_mask |= std::uint32_t {1} << index;
                }
            }
            if (reusable_mask == 0) {
                continue;
            }
            auto& reuse_report =
                report(LuauOptimizationPass::ReuseQueryUserdata);
            auto& chunk_report =
                report(LuauOptimizationPass::ChunkQueryIteration);
            ++reuse_report.candidates;
            ++chunk_report.candidates;
            const bool reuse_enabled =
                enabled(LuauOptimizationPass::ReuseQueryUserdata);
            const bool chunk_enabled =
                enabled(LuauOptimizationPass::ChunkQueryIteration);
            if (reuse_enabled) {
                ++reuse_report.applied;
            }
            const std::uint32_t effective_mask =
                reuse_enabled ? reusable_mask : 0;
            std::size_t property_accesses = 0;
            for (const auto& [local, index] : loop.fields) {
                (void)index;
                if (m_reusable.at(local)) {
                    property_accesses += m_reusable_property_accesses[local];
                }
            }
            const auto* source =
                loop.statement->values.data[0]->as<AstExprLocal>();
            if (chunk_enabled && loop.query_field_count <= 32 &&
                loop.chunk_safe && property_accesses <= 2) {
                ++chunk_report.applied;
                add_chunk_loop_patches(loop, *source, effective_mask);
            } else if (reuse_enabled) {
                m_patches.add(
                    loop.statement->values.data[0]->location,
                    "__ets_reuse_query(" +
                        std::string(name_view(source->local->name)) + ", " +
                        std::to_string(effective_mask) + ")",
                    luau_optimization_pass_name(
                        LuauOptimizationPass::ReuseQueryUserdata
                    )
                );
            }
        }
    }

  private:
    std::unordered_map<const AstLocal*, KnownReflectedLocal> m_known;
    std::unordered_map<
        const AstLocal*,
        std::vector<Optional<KnownReflectedLocal>>>
        m_queries;
    std::unordered_map<const AstLocal*, bool> m_reusable;
    std::unordered_map<const AstLocal*, std::size_t>
        m_reusable_property_accesses;
    std::vector<ReusableQueryLoop> m_reusable_loops;
    std::unordered_map<const AstLocal*, ScalarizedPropertyAlias>
        m_scalar_aliases;
    std::vector<const AstLocal*> m_scalar_alias_order;
    std::unordered_set<const AstExprIndexName*> m_scalar_alias_initializers;
    std::vector<PendingAliasPropertyAccess> m_pending_alias_accesses;
    std::vector<LuauPropertyPathDecl>& m_paths;
    SourcePatchSet& m_patches;
    std::vector<LuauOptimizationPassReport>& m_reports;
    LuauOptimizationPasses m_optimization_passes;

    [[nodiscard]] bool enabled(LuauOptimizationPass pass) const {
        return m_optimization_passes.contains(pass);
    }

    LuauOptimizationPassReport& report(LuauOptimizationPass pass) {
        return m_reports[static_cast<std::size_t>(pass)];
    }

    void add_generic_property_patch(
        Luau::Location location,
        const AstLocal* local,
        const std::vector<std::string>& properties,
        LuauOptimizationPass owner
    ) {
        std::string replacement {name_view(local->name)};
        for (const auto& property : properties) {
            replacement.push_back('.');
            replacement.append(property);
        }
        m_patches.add(
            location,
            std::move(replacement),
            luau_optimization_pass_name(owner)
        );
    }

    void add_chunk_loop_patches(
        const ReusableQueryLoop& loop,
        const AstExprLocal& source,
        std::uint32_t mask
    ) {
        // Keep the original body inside a numeric row loop. `continue` and
        // `return` retain their meaning; loops with `break` are filtered out
        // before this lowering because they need the original loop boundary.
        const auto* statement = loop.statement;
        const std::string id = std::to_string(statement->location.begin.line) +
                               "_" +
                               std::to_string(statement->location.begin.column);
        const std::string prefix = "__ets_chunk_" + id;
        const std::string refill = prefix + "_refill";
        const std::string count = prefix + "_count";
        const std::string index = prefix + "_index";

        std::vector<std::string> field_tables;
        field_tables.reserve(loop.query_field_count);
        for (std::size_t field = 0; field < loop.query_field_count; ++field) {
            field_tables.push_back(prefix + "_field_" + std::to_string(field));
        }

        std::string header = "do\nlocal " + refill;
        for (const auto& field : field_tables) {
            header.append(", ");
            header.append(field);
        }
        header.append(" = __ets_chunk_query(");
        header.append(name_view(source.local->name));
        header.append(", ");
        header.append(std::to_string(mask));
        header.append(")\nwhile true do\nlocal ");
        header.append(count);
        header.append(" = ");
        header.append(refill);
        header.append("()\nif ");
        header.append(count);
        header.append(" == 0 then break end\nfor ");
        header.append(index);
        header.append(" = 1, ");
        header.append(count);
        header.append(" do\nlocal ");
        for (std::size_t variable = 0; variable < statement->vars.size;
             ++variable) {
            if (variable != 0) {
                header.append(", ");
            }
            header.append(name_view(statement->vars.data[variable]->name));
        }
        header.append(" = ");
        for (std::size_t variable = 0; variable < statement->vars.size;
             ++variable) {
            if (variable != 0) {
                header.append(", ");
            }
            if (variable < field_tables.size()) {
                header.append(field_tables[variable]);
                header.push_back('[');
                header.append(index);
                header.push_back(']');
            } else {
                header.append("nil");
            }
        }

        auto header_location = statement->location;
        header_location.end = statement->body->location.begin;
        m_patches.add(
            header_location,
            std::move(header),
            luau_optimization_pass_name(
                LuauOptimizationPass::ChunkQueryIteration
            )
        );

        auto suffix_location = statement->location;
        suffix_location.begin = statement->body->location.end;
        m_patches.add(
            suffix_location,
            "end\nend\nend",
            luau_optimization_pass_name(
                LuauOptimizationPass::ChunkQueryIteration
            )
        );
    }

    bool add_property_patch(
        Luau::Location location,
        const AstLocal* local,
        TypeId root_type,
        TypeId leaf_type,
        const std::vector<std::string>& properties
    ) {
        std::string signature = std::to_string(root_type.id());
        for (const auto& property : properties) {
            signature.push_back('\0');
            signature.append(property);
        }
        std::string reversed(signature.rbegin(), signature.rend());
        const std::string atom_name =
            "__ets_p" + std::to_string(stable_name_hash(signature)) + "_" +
            std::to_string(stable_name_hash(reversed));

        const auto existing = std::ranges::find(
            m_paths,
            atom_name,
            &LuauPropertyPathDecl::atom_name
        );
        if (existing == m_paths.end()) {
            m_paths.push_back(
                LuauPropertyPathDecl {
                    .atom_name = atom_name,
                    .root_type = root_type,
                    .leaf_type = leaf_type,
                    .properties = properties,
                }
            );
        } else if (
            existing->root_type != root_type ||
            existing->leaf_type != leaf_type ||
            existing->properties != properties
        ) {
            return true;
        }

        m_patches.add(
            location,
            std::string(name_view(local->name)) + "." + atom_name,
            luau_optimization_pass_name(
                LuauOptimizationPass::FlattenPropertyPaths
            )
        );
        ++report(LuauOptimizationPass::FlattenPropertyPaths).applied;
        return false;
    }

    static bool
    append_type_token_name(const AstExpr& expression, std::string& name) {
        if (const auto* global = expression.as<AstExprGlobal>()) {
            name.append(name_view(global->name));
            return true;
        }
        if (const auto* member = expression.as<AstExprIndexName>()) {
            if (!append_type_token_name(*member->expr, name)) {
                return false;
            }
            name.append("::");
            name.append(name_view(member->index));
            return true;
        }
        return false;
    }

    static Optional<KnownReflectedLocal>
    constructed_value(const AstExpr& expression) {
        const auto* call = expression.as<AstExprCall>();
        const auto* constructor =
            call != nullptr ? call->func->as<AstExprIndexName>() : nullptr;
        if (constructor == nullptr || call->self ||
            name_view(constructor->index) != "new") {
            return nullopt;
        }
        std::string type_name;
        if (!append_type_token_name(*constructor->expr, type_name)) {
            return nullopt;
        }
        auto type = Registry::instance().try_get_type(type_name);
        if (!type || !Registry::instance().try_get_cls(type->id())) {
            return nullopt;
        }
        return KnownReflectedLocal {.type = type->id(), .writable = true};
    }

    Optional<ResolvedPropertyChain>
    resolve_chain(const AstExpr& expression) const {
        const AstExpr* current = &expression;
        std::vector<std::string> reversed;
        while (const auto* member = current->as<AstExprIndexName>()) {
            reversed.emplace_back(name_view(member->index));
            current = member->expr;
        }
        const auto* root = current->as<AstExprLocal>();
        if (root == nullptr || reversed.empty()) {
            return nullopt;
        }
        const auto known = m_known.find(root->local);
        if (known == m_known.end()) {
            return nullopt;
        }

        std::ranges::reverse(reversed);
        const auto scalar_alias = m_scalar_aliases.find(root->local);
        const KnownReflectedLocal root_value =
            scalar_alias != m_scalar_aliases.end() ?
                scalar_alias->second.root_value :
                known->second;
        TypeId current_type = scalar_alias != m_scalar_aliases.end() ?
                                  scalar_alias->second.value_type :
                                  known->second.type;
        for (const auto& name : reversed) {
            auto cls = Registry::instance().try_get_cls(current_type);
            if (!cls) {
                return nullopt;
            }
            auto property = cls->try_get_property(name);
            if (!property) {
                return nullopt;
            }
            current_type = property->type_id();
        }
        std::vector<std::string> properties;
        if (scalar_alias != m_scalar_aliases.end()) {
            properties = scalar_alias->second.properties;
        }
        properties.insert(properties.end(), reversed.begin(), reversed.end());
        return ResolvedPropertyChain {
            .root = root,
            .root_value = root_value,
            .leaf_type = current_type,
            .properties = std::move(properties),
            .local_properties = std::move(reversed),
            .scalar_alias =
                scalar_alias != m_scalar_aliases.end() ? root->local : nullptr,
        };
    }
};

const std::vector<DynamicSystemParamDeclPtr>* params_for(
    const std::vector<LuauFunctionDecl>& functions,
    std::string_view name
) {
    const auto function =
        std::ranges::find(functions, name, &LuauFunctionDecl::name);
    return function != functions.end() ? &function->params : nullptr;
}

} // namespace

PropertyLoweringResult PropertyLoweringPass::run(
    const Luau::AstStatBlock& root,
    const std::vector<LuauFunctionDecl>& functions,
    LuauOptimizationPasses optimization_passes
) const {
    PropertyLoweringResult result;
    result.optimization_report.reserve(
        static_cast<std::size_t>(LuauOptimizationPass::Count)
    );
    for (std::uint8_t index = 0;
         index < static_cast<std::uint8_t>(LuauOptimizationPass::Count);
         ++index) {
        const auto pass = static_cast<LuauOptimizationPass>(index);
        result.optimization_report.push_back(
            LuauOptimizationPassReport {
                .pass = pass,
                .enabled = optimization_passes.contains(pass),
            }
        );
    }
    for (Luau::AstStat* statement : root.body) {
        const auto* function = statement->as<AstStatLocalFunction>();
        if (function == nullptr) {
            continue;
        }
        const auto* params =
            params_for(functions, name_view(function->name->name));
        if (params == nullptr) {
            continue;
        }
        BorrowAnalysis analysis {
            *function->func,
            *params,
            result.property_paths,
            result.source_patches,
            result.optimization_report,
            optimization_passes,
        };
        function->func->body->visit(&analysis);
        analysis.finish();
    }
    return result;
}

} // namespace ets::detail::luau_compiler
