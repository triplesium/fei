#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace fei::refl_test {

struct TestStruct {
    int a;
    float b;
};

struct HeapValStruct {
    int arr[32] {};
    double values[8] {};
};

class StdoutCapture {
  private:
    std::ostringstream m_stream;
    std::streambuf* m_old_buffer;

  public:
    StdoutCapture() : m_old_buffer(std::cout.rdbuf(m_stream.rdbuf())) {}
    ~StdoutCapture() { std::cout.rdbuf(m_old_buffer); }

    std::string str() const { return m_stream.str(); }
};

} // namespace fei::refl_test
