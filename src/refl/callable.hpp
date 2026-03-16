#pragma once
#include "refl/qual_type.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"

#include <memory>
#include <string>
#include <vector>

namespace fei {

class Val;

class Param {
  private:
    std::string m_name;
    TypeId m_type_id;

  public:
    Param() = default;
    Param(std::string name, TypeId type_id);

    TypeId type_id() const;

    const std::string& name() const;

    void set_name(const std::string& name);
};

class ReturnValue {
  private:
    std::shared_ptr<Val> m_val;
    Ref m_ref;

  public:
    enum class Kind { Void, Value, Reference } m_kind;

    ReturnValue();

    ReturnValue(Val val);

    ReturnValue(Ref ref);

    Kind kind() const;

    Val& value();

    Ref ref() const;

    bool is_value() const;
    bool is_ref() const;
    bool is_void() const;
};

class Callable {
  public:
    Callable(
        std::string name,
        const std::vector<Param>& params,
        const QualType& return_type
    );
    virtual ~Callable() = default;

    bool validate(const std::vector<Ref>& args) const;

    virtual ReturnValue invoke_variadic(const std::vector<Ref>& args) const = 0;

    ReturnValue invoke(Ref arg0) const;

    ReturnValue invoke(Ref arg0, Ref arg1) const;

    ReturnValue invoke(Ref arg0, Ref arg1, Ref arg2) const;

    ReturnValue invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3) const;

    ReturnValue invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3, Ref arg4) const;

    ReturnValue
    invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3, Ref arg4, Ref arg5) const;

    const std::string& name() const;
    const std::vector<Param>& params() const;
    QualType return_type() const;

  protected:
    std::string m_name;
    std::vector<Param> m_params;
    QualType m_return_type;
};

} // namespace fei
