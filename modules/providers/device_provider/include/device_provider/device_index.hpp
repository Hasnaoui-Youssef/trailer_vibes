#ifndef TRAILER_PROVIDERS_DEVICE_PROVIDER_DEVICE_INDEX_HPP_
#define TRAILER_PROVIDERS_DEVICE_PROVIDER_DEVICE_INDEX_HPP_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace providers::device {

struct DeviceIndexEntry {
    std::string core;
    std::string die;
    std::uint32_t ram_kb = 0;
    std::uint32_t flash_kb = 0;
    std::uint32_t core_count = 1;
};

class DeviceIndex {
public:
    static std::optional<DeviceIndex> Load(const std::filesystem::path &tsv_path);

    std::optional<DeviceIndexEntry> Find(const std::string &device_name) const;

private:
    std::unordered_map<std::string, DeviceIndexEntry> m_entries;
};

}  // namespace providers::device

#endif  // TRAILER_PROVIDERS_DEVICE_PROVIDER_DEVICE_INDEX_HPP_
