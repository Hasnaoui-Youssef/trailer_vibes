#ifndef TRAILER_CONFIG_PARSER_SRC_CONFIG_PARSER_DETAIL_HPP_
#define TRAILER_CONFIG_PARSER_SRC_CONFIG_PARSER_DETAIL_HPP_

#include <string>
#include <string_view>

namespace config::detail {

// Formats a parse error with enough context (source path, offending field)
// for an external caller to react to. Shared across all ConfigParser
// Parse* entry points, regardless of input format, so format-specific
// translation units (e.g. config_parser_json.cc) stay consistent.
std::string MakeError(std::string_view path, std::string_view message);

}  // namespace config::detail

#endif  // TRAILER_CONFIG_PARSER_SRC_CONFIG_PARSER_DETAIL_HPP_
