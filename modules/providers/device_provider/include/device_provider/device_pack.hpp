#ifndef TRAILER_PROVIDERS_DEVICE_PROVIDER_DEVICE_PACK_HPP_
#define TRAILER_PROVIDERS_DEVICE_PROVIDER_DEVICE_PACK_HPP_

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "device_provider/memory_map.hpp"
#include "device_xml/svd_model.hpp"

namespace providers::device {

class DevicePack {
public:
    static DevicePack Load(const std::string &device_name);

    const MemoryMap &Memory() const { return m_memory; }
    const std::optional<device_xml::Device> &CorePeripherals() const { return m_core; }

private:
    MemoryMap m_memory;
    std::optional<device_xml::Device> m_core;
};

std::expected<device_xml::Device, std::string> LoadDeviceSvd(const std::filesystem::path &svd_path);

}  // namespace providers::device

#endif  // TRAILER_PROVIDERS_DEVICE_PROVIDER_DEVICE_PACK_HPP_
