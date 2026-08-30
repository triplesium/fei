#include "wasm_transport.hpp"

#include <cstring>
#include <stdexcept>

namespace ets::lsp {
namespace {

[[nodiscard]] std::string message_payload(const std::string& data) {
    constexpr std::string_view separator {"\r\n\r\n"};
    const auto body = data.find(separator);
    if (body == std::string::npos) {
        throw std::runtime_error("Luau LSP emitted a message without headers");
    }
    return data.substr(body + separator.size());
}

} // namespace

void WasmTransport::send(const std::string& data) {
    auto payload = message_payload(data);
    std::scoped_lock lock {m_output_mutex};
    m_output.push(std::move(payload));
}

void WasmTransport::read(char* buffer, const unsigned int length) {
    std::unique_lock lock {m_input_mutex};
    m_input_ready.wait(lock, [this, length] {
        return m_input.size() - m_input_offset >= length;
    });
    std::memcpy(buffer, m_input.data() + m_input_offset, length);
    m_input_offset += length;
    compact_input();
}

bool WasmTransport::readLine(std::string& output) {
    std::unique_lock lock {m_input_mutex};
    std::size_t newline = std::string::npos;
    m_input_ready.wait(lock, [this, &newline] {
        newline = m_input.find('\n', m_input_offset);
        return newline != std::string::npos;
    });
    output.assign(m_input.data() + m_input_offset, newline - m_input_offset);
    m_input_offset = newline + 1;
    compact_input();
    return true;
}

void WasmTransport::push(const std::string_view json) {
    auto frame = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" +
                 std::string {json};
    {
        std::scoped_lock lock {m_input_mutex};
        m_input.append(frame);
    }
    m_input_ready.notify_one();
}

std::optional<std::string> WasmTransport::pop_output() {
    std::scoped_lock lock {m_output_mutex};
    if (m_output.empty()) {
        return std::nullopt;
    }
    auto result = std::move(m_output.front());
    m_output.pop();
    return result;
}

void WasmTransport::compact_input() {
    if (m_input_offset == m_input.size()) {
        m_input.clear();
        m_input_offset = 0;
        return;
    }
    if (m_input_offset >= 64 * 1024) {
        m_input.erase(0, m_input_offset);
        m_input_offset = 0;
    }
}

} // namespace ets::lsp
