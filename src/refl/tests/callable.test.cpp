#include "refl/cls.hpp"
#include "refl/constructor.hpp"
#include "refl/method.hpp"
#include "refl/property.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

enum class TestEnum { One = 1, Two = 2 };

struct CallableFixture {
    int value {0};

    CallableFixture() = default;
    explicit CallableFixture(int value) : value(value) {}

    int add(int rhs) { return value + rhs; }
    int read() const { return value; }
    void set(int next) { value = next; }
    static int sum(int lhs, int rhs) { return lhs + rhs; }
    int enum_value(TestEnum value) const { return static_cast<int>(value); }
    int read_ptr(const int* ptr) const { return *ptr; }
    void write_ptr(int* ptr) const { *ptr = value; }
};

struct PointerCtorFixture {
    int* ptr {nullptr};

    explicit PointerCtorFixture(int* ptr) : ptr(ptr) {}
};

} // namespace

TEST_CASE("Property validates object constness and value types", "[refl][property]") {
    auto& cls = Registry::instance()
                    .register_cls<CallableFixture>()
                    .add_property("value", &CallableFixture::value);
    auto* prop = cls.get_property("value");
    REQUIRE(prop != nullptr);

    CallableFixture obj {3};
    int next = 9;
    float wrong = 2.0f;

    REQUIRE(prop->get(make_ref(obj)).get<int>() == 3);
    REQUIRE(prop->set(make_ref(obj), make_ref(next)));
    REQUIRE(obj.value == 9);
    REQUIRE_FALSE(prop->set(make_ref(obj), make_ref(wrong)));

    const CallableFixture const_obj {7};
    auto const_value = prop->get(make_ref(const_obj));
    REQUIRE(const_value.is_const());
    REQUIRE(const_value.get_const<int>() == 7);
    REQUIRE_FALSE(prop->set(make_ref(const_obj), make_ref(next)));
}

TEST_CASE("Method validates instances and argument types", "[refl][method]") {
    auto& registry = Registry::instance();
    registry.register_enum<TestEnum>();
    auto& cls = registry.register_cls<CallableFixture>()
                    .add_method("add", &CallableFixture::add)
                    .add_method("read", &CallableFixture::read)
                    .add_method("set", &CallableFixture::set)
                    .add_method("sum", &CallableFixture::sum)
                    .add_method("enum_value", &CallableFixture::enum_value)
                    .add_method("read_ptr", &CallableFixture::read_ptr)
                    .add_method("write_ptr", &CallableFixture::write_ptr);

    auto method_count = cls.get_methods("add").size();
    cls.add_method("add", &CallableFixture::add);
    REQUIRE(cls.get_methods("add").size() == method_count);

    CallableFixture obj {5};
    int rhs = 4;
    float wrong = 4.0f;

    auto* add = cls.get_method("add", {type_id<int>()});
    REQUIRE(add != nullptr);
    auto add_ret = add->invoke(make_ref(obj), make_ref(rhs));
    REQUIRE(add_ret.is_value());
    REQUIRE(add_ret.value().get<int>() == 9);

    auto wrong_ret = add->invoke(make_ref(obj), make_ref(wrong));
    REQUIRE(wrong_ret.is_void());

    const CallableFixture const_obj {8};
    auto* read = cls.get_method("read", {});
    REQUIRE(read != nullptr);
    auto read_ret = read->invoke(make_ref(const_obj));
    REQUIRE(read_ret.is_value());
    REQUIRE(read_ret.value().get<int>() == 8);

    auto const_add_ret = add->invoke(make_ref(const_obj), make_ref(rhs));
    REQUIRE(const_add_ret.is_void());

    int lhs = 2;
    auto* sum = cls.get_method("sum", {type_id<int>(), type_id<int>()});
    REQUIRE(sum != nullptr);
    auto sum_ret = sum->invoke(make_ref(lhs), make_ref(rhs));
    REQUIRE(sum_ret.is_value());
    REQUIRE(sum_ret.value().get<int>() == 6);

    int enum_arg = 2;
    auto* enum_method = cls.get_method("enum_value", {type_id<int>()});
    REQUIRE(enum_method != nullptr);
    auto enum_ret = enum_method->invoke(make_ref(obj), make_ref(enum_arg));
    REQUIRE(enum_ret.is_value());
    REQUIRE(enum_ret.value().get<int>() == 2);

    int pointed = 11;
    auto* read_ptr = cls.get_method("read_ptr", {type_id<int>()});
    REQUIRE(read_ptr != nullptr);
    auto read_ptr_ret = read_ptr->invoke(make_ref(obj), make_ref(pointed));
    REQUIRE(read_ptr_ret.is_value());
    REQUIRE(read_ptr_ret.value().get<int>() == 11);

    auto* write_ptr = cls.get_method("write_ptr", {type_id<int>()});
    REQUIRE(write_ptr != nullptr);
    auto write_ptr_ret = write_ptr->invoke(make_ref(obj), make_ref(pointed));
    REQUIRE(write_ptr_ret.is_void());
    REQUIRE(pointed == 5);

    const int const_pointed = 13;
    auto const_write_ret =
        write_ptr->invoke(make_ref(obj), make_ref(const_pointed));
    REQUIRE(const_write_ret.is_void());
    REQUIRE(pointed == 5);
}

TEST_CASE("Constructor validates argument types", "[refl][constructor]") {
    auto& cls = Registry::instance()
                    .register_cls<CallableFixture>()
                    .add_constructor<CallableFixture, int>();

    int value = 12;
    float wrong = 12.0f;

    auto* ctor = cls.get_constructor({type_id<int>()});
    REQUIRE(ctor != nullptr);

    auto ret = ctor->invoke_variadic({make_ref(value)});
    REQUIRE(ret.is_value());
    REQUIRE(ret.value().get<CallableFixture>().value == 12);

    auto wrong_ret = ctor->invoke_variadic({make_ref(wrong)});
    REQUIRE(wrong_ret.is_void());

    auto& ptr_cls = Registry::instance()
                        .register_cls<PointerCtorFixture>()
                        .add_constructor<PointerCtorFixture, int*>();
    auto* ptr_ctor = ptr_cls.get_constructor({type_id<int>()});
    REQUIRE(ptr_ctor != nullptr);

    int pointed = 7;
    auto ptr_ret = ptr_ctor->invoke_variadic({make_ref(pointed)});
    REQUIRE(ptr_ret.is_value());
    REQUIRE(ptr_ret.value().get<PointerCtorFixture>().ptr == &pointed);

    const int const_pointed = 9;
    auto const_ptr_ret = ptr_ctor->invoke_variadic({make_ref(const_pointed)});
    REQUIRE(const_ptr_ret.is_void());
}
