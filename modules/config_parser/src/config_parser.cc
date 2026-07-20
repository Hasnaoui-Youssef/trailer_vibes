#include "config_parser_detail.hpp"

namespace config::detail {

std::string MakeError(std::string_view path, std::string_view message) {
    return std::string("config: ").append(path).append(": ").append(message);
}

}  // namespace config::detail
