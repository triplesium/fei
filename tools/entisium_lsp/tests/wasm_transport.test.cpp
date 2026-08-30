#include "wasm_transport.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("WASM transport frames browser messages", "[lsp][wasm]") {
    ets::lsp::WasmTransport transport;
    transport.push(R"({"jsonrpc":"2.0","method":"initialized"})");

    std::string line;
    REQUIRE(transport.readLine(line));
    CHECK(line == "Content-Length: 40\r");
    REQUIRE(transport.readLine(line));
    CHECK(line == "\r");

    std::string body(40, '\0');
    transport.read(body.data(), static_cast<unsigned int>(body.size()));
    CHECK(body == R"({"jsonrpc":"2.0","method":"initialized"})");
}

TEST_CASE("WASM transport exposes JSON payloads", "[lsp][wasm]") {
    ets::lsp::WasmTransport transport;
    transport.send(
        "Content-Length: 38\r\n\r\n"
        R"({"jsonrpc":"2.0","id":1,"result":null})"
    );

    CHECK(
        transport.pop_output() == R"({"jsonrpc":"2.0","id":1,"result":null})"
    );
    CHECK_FALSE(transport.pop_output());
}
