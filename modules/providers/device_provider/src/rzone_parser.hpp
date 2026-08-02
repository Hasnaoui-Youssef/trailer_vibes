#ifndef TRAILER_PROVIDERS_DEVICE_PROVIDER_SRC_RZONE_PARSER_HPP_
#define TRAILER_PROVIDERS_DEVICE_PROVIDER_SRC_RZONE_PARSER_HPP_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "device_provider/memory_map.hpp"

namespace providers::device {

std::optional<MemoryMap> LoadRzoneMemoryMap(const std::filesystem::path &rzone_dir, const std::string &die,
                                             std::uint32_t ram_kb, std::uint32_t flash_kb,
                                             std::uint32_t core_count);

}  // namespace providers::device

#endif  // TRAILER_PROVIDERS_DEVICE_PROVIDER_SRC_RZONE_PARSER_HPP_
