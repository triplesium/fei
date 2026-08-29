#pragma once

#include "scripting/compiler.hpp"

#include <Luau/Parser.h>

namespace ets::detail::luau_compiler {

class CompilationSession;

class ParsedModule final {
  public:
    ParsedModule(const ParsedModule&) = delete;
    ParsedModule& operator=(const ParsedModule&) = delete;

    const LuauScriptSource& source() const;
    Luau::AstStatBlock& root();
    const Luau::AstStatBlock& root() const;

  private:
    friend class CompilationSession;

    ParsedModule() = default;

    const LuauScriptSource* m_source {nullptr};
    Luau::AstStatBlock* m_root {nullptr};
};

class CompilationSession final {
  public:
    explicit CompilationSession(
        const LuauScriptSource& source,
        LuauCompileOptions options = {}
    );
    CompilationSession(const CompilationSession&) = delete;
    CompilationSession& operator=(const CompilationSession&) = delete;

    Result<ParsedModule&, LuauScriptError> parse();

    const LuauScriptSource& source() const;
    const LuauCompileOptions& options() const;

  private:
    LuauScriptError parse_error(const Luau::ParseError& error) const;

    const LuauScriptSource* m_source;
    LuauCompileOptions m_options;
    Luau::Allocator m_allocator;
    Luau::AstNameTable m_names;
    bool m_parse_attempted {false};
    LuauScriptError m_parse_error;
    ParsedModule m_parsed_module;
};

} // namespace ets::detail::luau_compiler
