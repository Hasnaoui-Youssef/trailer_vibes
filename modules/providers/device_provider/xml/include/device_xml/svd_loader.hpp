#ifndef TRAILER_PROVIDERS_DEVICE_PROVIDER_XML_SVD_LOADER_HPP_
#define TRAILER_PROVIDERS_DEVICE_PROVIDER_XML_SVD_LOADER_HPP_

#include <expected>
#include <filesystem>
#include <string>

#include "device_xml/svd_model.hpp"

namespace device_xml {

std::expected<Device, std::string> LoadSvd(const std::filesystem::path &path);

}  // namespace device_xml

#endif  // TRAILER_PROVIDERS_DEVICE_PROVIDER_XML_SVD_LOADER_HPP_
