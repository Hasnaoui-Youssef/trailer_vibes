#include "device_provider/device_index.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace providers::device {

namespace {

std::vector<std::string> SplitTab(const std::string &line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

}  // namespace

std::optional<DeviceIndex> DeviceIndex::Load(const std::filesystem::path &tsv_path) {
    std::ifstream file(tsv_path);
    if (!file.is_open()) return std::nullopt;

    DeviceIndex index;
    std::string line;
    std::getline(file, line);  // header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> fields = SplitTab(line);
        if (fields.size() != 6) continue;

        DeviceIndexEntry entry;
        entry.core = fields[1];
        entry.die = fields[2];
        entry.ram_kb = static_cast<std::uint32_t>(std::stoul(fields[3]));
        entry.flash_kb = static_cast<std::uint32_t>(std::stoul(fields[4]));
        entry.core_count = static_cast<std::uint32_t>(std::stoul(fields[5]));
        index.m_entries.emplace(fields[0], std::move(entry));
    }

    return index;
}

std::optional<DeviceIndexEntry> DeviceIndex::Find(const std::string &device_name) const {
    const auto it = m_entries.find(device_name);
    if (it == m_entries.end()) return std::nullopt;
    return it->second;
}

}  // namespace providers::device
