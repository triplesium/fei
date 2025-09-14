#include "log.hpp"

#include <iostream>

namespace fei::detail {

std::string make_log_prefix(LogLevel level, std::source_location loc) {
    auto get_filename = [](std::source_location location) {
        const char* full_name = location.file_name();
        const char* last_slash = strrchr(full_name, '/') ?
                                     strrchr(full_name, '/') :
                                     strrchr(full_name, '\\');
        const char* filename = last_slash ? last_slash + 1 : full_name;
        return filename;
    };
    using namespace std::literals;
    return std::format(
        "[{}] [{}:{}] ",
        LOG_LEVEL_STR[static_cast<int>(level)],
        get_filename(loc),
        loc.line()
    );
}

void log(LogLevel level, const FormatString& format, std::format_args args) {
    const auto& loc = format.loc;
    std::cout << make_log_prefix(level, loc) << std::vformat(format.str, args)
              << "\n";
}

} // namespace fei::detail
