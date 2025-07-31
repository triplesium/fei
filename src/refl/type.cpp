#include "refl/type.hpp"

namespace fei {

bool Type::is_number() const {
    return is_integral() || is_floating_point();
}

bool Type::is_integral() const {
    return m_id == type_id<int>() || m_id == type_id<signed char>() ||
           m_id == type_id<unsigned char>() || m_id == type_id<short int>() ||
           m_id == type_id<long int>() || m_id == type_id<long long int>() ||
           m_id == type_id<unsigned short int>() ||
           m_id == type_id<unsigned long int>() ||
           m_id == type_id<unsigned long long int>();
}

bool Type::is_floating_point() const {
    return m_id == type_id<float>() || m_id == type_id<double>() ||
           m_id == type_id<long double>();
}

std::string Type::stripped_name() const {
    auto pos = m_name.find_last_of("::");
    return (pos == std::string::npos) ? m_name : m_name.substr(pos + 1);
}

} // namespace fei
