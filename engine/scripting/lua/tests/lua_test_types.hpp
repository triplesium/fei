#pragma once

#include "base/result.hpp"
#include "scripting_lua/runtime.hpp"

#include <string_view>

namespace fei {

enum class ScriptTestEnum { Idle = 1, Active = 2 };

struct ScriptTestError {
    int code {0};
};

struct ScriptBareType {
    int value {0};
};

struct ScriptTestReceiver {
    ScriptTestEnum mode {ScriptTestEnum::Idle};
    int value {0};
    int method_calls {0};
    float scale {0.0f};
    double precise {0.0};
    std::string_view view;

    ScriptTestReceiver();
    explicit ScriptTestReceiver(int value);

    void set_value(int next);
    void set_mode(ScriptTestEnum next);
    void copy_to(ScriptTestReceiver& target) const;
    void set_scaled(float factor);
    void set_text_size(std::string_view text);
    void choose_number(float);
    void choose_number(double);
    Result<int, ScriptTestError> result_value(bool succeed) const;
    Status<ScriptTestError> status_value(bool succeed) const;
};

void register_script_test_metadata();
void register_transform_script_metadata();

LuaRuntime make_test_runtime();

} // namespace fei
