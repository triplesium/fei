#pragma once
#include "refl/qual_type.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <string>
#include <vector>

namespace fei {

class Param {
  private:
    std::string m_name;
    TypeId m_type_id;

  public:
    Param() = default;
    Param(std::string name, TypeId type_id) :
        m_name(name), m_type_id(type_id) {}

    TypeId type_id() const { return m_type_id; }

    const std::string& name() const { return m_name; }

    void set_name(const std::string& name) { m_name = name; }
};

class ReturnValue {
  private:
    Val m_val;
    Ref m_ref;

  public:
    enum class Kind { Void, Value, Reference } m_kind;

    ReturnValue() : m_kind(Kind::Void) {}

    ReturnValue(Val val) : m_val(val), m_kind(Kind::Value) {}

    ReturnValue(Ref ref) : m_ref(ref), m_kind(Kind::Reference) {}

    Kind kind() const { return m_kind; }

    Val& value() {
        assert(m_kind == Kind::Value);
        return m_val;
    }

    Ref ref() const {
        assert(m_kind == Kind::Reference || m_kind == Kind::Value);
        if (m_kind == Kind::Value) {
            return m_val.ref();
        } else {
            return m_ref;
        }
    }

    bool is_value() const { return m_kind == Kind::Value; }
    bool is_ref() const { return m_kind == Kind::Reference; }
    bool is_void() const { return m_kind == Kind::Void; }
};

class Callable {
  public:
    Callable(
        std::string name,
        const std::vector<Param>& params,
        const QualType& return_type
    ) : m_name(name), m_params(params), m_return_type(return_type) {}
    virtual ~Callable() = default;

    bool validate(const std::vector<Ref>& args) const {
        if (args.size() != m_params.size())
            return false;
        return true;
    }

    virtual ReturnValue invoke_variadic(const std::vector<Ref>& args) const = 0;

    ReturnValue invoke(Ref arg0) const { return invoke_variadic({arg0}); }

    ReturnValue invoke(Ref arg0, Ref arg1) const {
        return invoke_variadic({arg0, arg1});
    }

    ReturnValue invoke(Ref arg0, Ref arg1, Ref arg2) const {
        return invoke_variadic({arg0, arg1, arg2});
    }

    ReturnValue invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3) const {
        return invoke_variadic({arg0, arg1, arg2, arg3});
    }

    ReturnValue invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3, Ref arg4) const {
        return invoke_variadic({arg0, arg1, arg2, arg3, arg4});
    }

    ReturnValue
    invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3, Ref arg4, Ref arg5) const {
        return invoke_variadic({arg0, arg1, arg2, arg3, arg4, arg5});
    }

    const std::string& name() const { return m_name; }
    const std::vector<Param>& params() const { return m_params; }
    QualType return_type() const { return m_return_type; }

  protected:
    std::string m_name;
    std::vector<Param> m_params;
    QualType m_return_type;
};

} // namespace fei
