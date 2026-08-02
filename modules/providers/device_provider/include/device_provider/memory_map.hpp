#ifndef TRAILER_PROVIDERS_DEVICE_PROVIDER_MEMORY_MAP_HPP_
#define TRAILER_PROVIDERS_DEVICE_PROVIDER_MEMORY_MAP_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace providers::device {

enum class MemoryKind : std::uint8_t {
    kRam,
    kRom,
    kExternal,
};

struct MemoryRegion {
    std::string name;
    std::string info;
    std::uint64_t start = 0;
    std::uint64_t size = 0;
    bool readable = false;
    bool writable = false;
    bool executable = false;
    MemoryKind kind = MemoryKind::kExternal;
};

class MemoryMap {
public:
    MemoryMap() = default;
    explicit MemoryMap(std::vector<MemoryRegion> regions);

    std::optional<MemoryRegion> Find(std::uint64_t address) const;
    const std::vector<MemoryRegion> &Regions() const { return m_regions; }
    bool Empty() const { return m_regions.empty(); }

    bool IsRam(std::uint64_t address) const;
    bool IsRom(std::uint64_t address) const;

private:
    std::vector<MemoryRegion> m_regions;
};

}  // namespace providers::device

#endif  // TRAILER_PROVIDERS_DEVICE_PROVIDER_MEMORY_MAP_HPP_
