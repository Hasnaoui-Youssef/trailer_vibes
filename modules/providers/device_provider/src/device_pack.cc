#include "device_provider/device_pack.hpp"

#include "device_provider/device_index.hpp"
#include "device_xml/svd_loader.hpp"
#include "resource_paths.hpp"
#include "rzone_parser.hpp"

namespace providers::device {

DevicePack DevicePack::Load(const std::string &device_name) {
    DevicePack pack;

    const std::filesystem::path resources = ResourcesRoot();
    if (resources.empty()) return pack;

    const std::optional<DeviceIndex> index = DeviceIndex::Load(resources / "device_index.tsv");
    if (!index) return pack;

    const std::optional<DeviceIndexEntry> entry = index->Find(device_name);
    if (!entry) return pack;

    std::optional<MemoryMap> memory =
        LoadRzoneMemoryMap(resources / "XML" / "Rzone", entry->die, entry->ram_kb, entry->flash_kb, entry->core_count);
    if (memory) pack.m_memory = std::move(*memory);

    if (!entry->core.empty()) {
        const std::filesystem::path core_svd = resources / "XML" / "Cores" / (entry->core + ".svd");
        std::expected<device_xml::Device, std::string> core = device_xml::LoadSvd(core_svd);
        if (core) pack.m_core = std::move(*core);
    }

    return pack;
}

std::expected<device_xml::Device, std::string> LoadDeviceSvd(const std::filesystem::path &svd_path) {
    return device_xml::LoadSvd(svd_path);
}

}  // namespace providers::device
