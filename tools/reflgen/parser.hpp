#pragma once

#include "model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fei::reflgen {

class HeaderParser {
  public:
    HeaderParser(
        std::vector<std::string> headers,
        std::vector<std::string> include_paths,
        bool verbose
    );

    [[nodiscard]] ParseResult parse();

  private:
    std::vector<std::string> headers_;
    std::vector<std::string> include_paths_;
    bool verbose_ = false;
};

} // namespace fei::reflgen
