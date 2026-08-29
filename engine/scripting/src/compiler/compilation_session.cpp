#include "compilation_session.hpp"

#include <string>
#include <utility>

namespace ets::detail::luau_compiler {

const LuauScriptSource& ParsedModule::source() const {
    return *m_source;
}

Luau::AstStatBlock& ParsedModule::root() {
    return *m_root;
}

const Luau::AstStatBlock& ParsedModule::root() const {
    return *m_root;
}

CompilationSession::CompilationSession(
    const LuauScriptSource& source,
    LuauCompileOptions options
) : m_source(&source), m_options(std::move(options)), m_names(m_allocator) {}

Result<ParsedModule&, LuauScriptError> CompilationSession::parse() {
    if (m_parsed_module.m_root != nullptr) {
        return m_parsed_module;
    }
    if (m_parse_attempted) {
        return failure(LuauScriptError {m_parse_error.message});
    }
    m_parse_attempted = true;

    auto parsed = Luau::Parser::parse(
        m_source->content.data(),
        m_source->content.size(),
        m_names,
        m_allocator
    );
    if (!parsed.errors.empty()) {
        m_parse_error = parse_error(parsed.errors.front());
        return failure(LuauScriptError {m_parse_error.message});
    }
    m_parsed_module.m_source = m_source;
    m_parsed_module.m_root = parsed.root;
    return m_parsed_module;
}

const LuauScriptSource& CompilationSession::source() const {
    return *m_source;
}

const LuauCompileOptions& CompilationSession::options() const {
    return m_options;
}

LuauScriptError
CompilationSession::parse_error(const Luau::ParseError& error) const {
    return LuauScriptError {
        m_source->name + ":" +
            std::to_string(error.getLocation().begin.line + 1) + ": " +
            error.getMessage(),
    };
}

} // namespace ets::detail::luau_compiler
