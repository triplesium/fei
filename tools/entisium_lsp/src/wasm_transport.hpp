#pragma once

#include "LSP/Transport/Transport.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>

namespace ets::lsp {

class WasmTransport final : public Transport {
  public:
    void send(const std::string& data) override;
    void read(char* buffer, unsigned int length) override;
    bool readLine(std::string& output) override;

    void push(std::string_view json);
    [[nodiscard]] std::optional<std::string> pop_output();

  private:
    void compact_input();

    std::mutex m_input_mutex;
    std::condition_variable m_input_ready;
    std::string m_input;
    std::size_t m_input_offset {0};

    std::mutex m_output_mutex;
    std::queue<std::string> m_output;
};

} // namespace ets::lsp
