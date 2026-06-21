#include "asset/io.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string_view>

using namespace fei;

namespace {

std::filesystem::path reader_test_path(std::string_view filename) {
    return std::filesystem::temp_directory_path() / filename;
}

} // namespace

TEST_CASE(
    "Reader keeps owned data valid across copies and moves",
    "[asset][io]"
) {
    Reader original(std::string_view("reader data"));

    Reader copied = original;
    Reader moved = std::move(copied);

    REQUIRE(original.as_string() == "reader data");
    REQUIRE(moved.as_string() == "reader data");

    Reader copy_assigned(std::string_view("old"));
    copy_assigned = original;
    REQUIRE(copy_assigned.as_string() == "reader data");

    Reader move_assigned(std::string_view("old"));
    move_assigned = std::move(copy_assigned);
    REQUIRE(move_assigned.as_string() == "reader data");
}

TEST_CASE("Reader try_from_file returns file contents", "[asset][io]") {
    auto path = reader_test_path("fei-reader-test.bin");
    std::filesystem::remove(path);
    {
        std::ofstream file(path, std::ios::binary);
        file << "file data";
    }

    auto reader = Reader::try_from_file(path);

    std::filesystem::remove(path);
    REQUIRE(reader);
    REQUIRE(reader->as_string() == "file data");
}

TEST_CASE("Reader try_from_file returns errors", "[asset][io]") {
    auto path = reader_test_path("fei-reader-missing.bin");
    std::filesystem::remove(path);

    auto reader = Reader::try_from_file(path);

    REQUIRE_FALSE(reader);
    REQUIRE(reader.error().path == path);
    REQUIRE(reader.error().message.contains("Failed to open file"));
}
