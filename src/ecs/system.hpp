#pragma once

#include "base/type_traits.hpp"

#include <tuple>
#include <utility>

namespace fei {

class World;

class SystemParam {
  public:
    ~SystemParam() = default;
    virtual void prepare(World& world) {}
};

class System {
  public:
    System() = default;
    virtual ~System() = default;

    virtual void run(World& world) = 0;
};

template<typename Func>
class FunctionSystem : public System {
  private:
    using ParamTypes = typename function_traits<Func>::args_tuple;
    Func func_;

  public:
    explicit FunctionSystem(Func func) : func_(func) {}

    void run(World& world) override {
        auto params = prepare_params<ParamTypes>(world);
        std::apply(func_, params);
    }

  private:
    template<typename Tuple>
    Tuple prepare_params(World& world) {
        return prepare_params_impl<Tuple>(
            world,
            std::make_index_sequence<std::tuple_size_v<Tuple>> {}
        );
    }

    template<typename Tuple, std::size_t... Is>
    Tuple prepare_params_impl(World& world, std::index_sequence<Is...>) {
        return std::make_tuple(
            prepare_param<std::tuple_element_t<Is, Tuple>>(world)...
        );
    }

    template<typename T>
    T prepare_param(World& world) {
        T param;
        param.prepare(world);
        return param;
    }
};

} // namespace fei
