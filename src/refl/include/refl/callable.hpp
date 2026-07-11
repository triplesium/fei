#pragma once
#include "base/result.hpp"
#include "refl/qual_type.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <string>
#include <vector>

namespace fei {

class Param {
  private:
    std::string m_name;
    QualType m_type;

  public:
    Param() = default;
    Param(std::string name, TypeId type_id);
    Param(std::string name, QualType type);

    static Param dynamic(std::string name);

    TypeId type_id() const;
    QualType type() const;
    bool is_dynamic() const;

    const std::string& name() const;

    void set_name(const std::string& name);
};

class ReturnItem {
  public:
    enum class Kind { Value, Reference };

    static ReturnItem value(Val value);
    static ReturnItem reference(Ref ref);

    Kind kind() const;
    Val& value();
    const Val& value() const;
    Ref ref() const;

    bool is_value() const;
    bool is_ref() const;

  private:
    Kind m_kind {Kind::Value};
    Val m_val;
    Ref m_ref;
};

class ReturnValue {
  public:
    enum class Kind { Void, Status, One, Many };

    ReturnValue() = default;
    ReturnValue(Val value);
    ReturnValue(Ref ref);

    static ReturnValue void_value();
    static ReturnValue status();
    static ReturnValue value(Val value);
    static ReturnValue reference(Ref ref);
    static ReturnValue many(std::vector<ReturnItem> items);

    Kind kind() const;
    const ReturnItem& item() const;
    ReturnItem& item();
    const std::vector<ReturnItem>& items() const;

    Val& value();
    const Val& value() const;
    Ref ref() const;

    bool is_value() const;
    bool is_ref() const;
    bool is_void() const;
    bool is_status() const;
    bool is_one() const;
    bool is_many() const;

  private:
    Kind m_kind {Kind::Void};
    ReturnItem m_item;
    std::vector<ReturnItem> m_items;
};

struct InvokeFailure {
    enum class Kind { InvalidCall, ReturnedError };

    static InvokeFailure invalid_call(std::string message);
    static InvokeFailure returned_error(Val error);

    Kind kind {Kind::InvalidCall};
    std::string message;
    Val error;
};

using InvokeResult = Result<ReturnValue, InvokeFailure>;

class Callable {
  public:
    Callable(
        std::string name,
        const std::vector<Param>& params,
        const QualType& return_type
    );
    virtual ~Callable() = default;

    bool validate(const std::vector<Ref>& args) const;

    virtual InvokeResult
    invoke_variadic(const std::vector<Ref>& args) const = 0;

    InvokeResult invoke(Ref arg0) const;

    InvokeResult invoke(Ref arg0, Ref arg1) const;

    InvokeResult invoke(Ref arg0, Ref arg1, Ref arg2) const;

    InvokeResult invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3) const;

    InvokeResult invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3, Ref arg4) const;

    InvokeResult
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
