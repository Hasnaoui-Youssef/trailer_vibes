#include "device_provider/memory_map.hpp"

#include <algorithm>

namespace providers::device {

MemoryMap::MemoryMap(std::vector<MemoryRegion> regions) : m_regions(std::move(regions)) {
    std::sort(m_regions.begin(), m_regions.end(),
              [](const MemoryRegion &a, const MemoryRegion &b) { return a.start < b.start; });
}

std::optional<MemoryRegion> MemoryMap::Find(std::uint64_t address) const {
    for (const MemoryRegion &region : m_regions) {
        if (address >= region.start && address < region.start + region.size) {
            return region;
        }
    }
    return std::nullopt;
}

bool MemoryMap::IsRam(std::uint64_t address) const {
    const std::optional<MemoryRegion> region = Find(address);
    return region.has_value() && region->kind == MemoryKind::kRam;
}

bool MemoryMap::IsRom(std::uint64_t address) const {
    const std::optional<MemoryRegion> region = Find(address);
    return region.has_value() && region->kind == MemoryKind::kRom;
}

}  // namespace providers::device
