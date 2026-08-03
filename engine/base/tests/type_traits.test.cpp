#include "base/type_traits.hpp"

#include "base/concepts.hpp"

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

TEST_CASE(
    "type traits describe callables and type packs",
    "[base][type_traits]"
) {
    using SampleFunction = double(int, const std::string&);
    using Function = fei::FunctionTraits<SampleFunction>;
    static_assert(std::is_same_v<Function::return_type, double>);
    static_assert(Function::arg_size == 2);
    static_assert(std::is_same_v<Function::arg_type<0>, int>);
    static_assert(std::is_same_v<Function::arg_type<1>, const std::string&>);

    auto lambda = [](int, float) -> bool {
        return true;
    };
    using Lambda = fei::FunctionTraits<decltype(lambda)>;
    static_assert(std::is_same_v<Lambda::return_type, bool>);
    static_assert(Lambda::arg_size == 2);

    using StdFunction = fei::FunctionTraits<std::function<void(std::string)>>;
    static_assert(std::is_same_v<StdFunction::return_type, void>);
    static_assert(std::is_same_v<StdFunction::arg_type<0>, std::string>);

    static_assert(fei::IndexInPack<float, int, float, double> == 1);
    static_assert(fei::AnyOf<int, float, int>);
    static_assert(fei::SpecializationOf<std::vector<int>, std::vector>);

    SUCCEED("compile-time traits are validated");
}
