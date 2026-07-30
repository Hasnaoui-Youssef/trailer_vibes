#ifndef TRAILER_PROVIDERS_PERIPHERAL_PROVIDER_XML_PARSER_SVD_PARSER_HPP_
#define TRAILER_PROVIDERS_PERIPHERAL_PROVIDER_XML_PARSER_SVD_PARSER_HPP_

#include <filesystem>
#include <pugixml.hpp>

#include <svd_peripheral.hpp>

namespace providers::svd {
class SvdParser {
public:
    SvdParser(const std::filesystem::path& path) : filePath_(path){}
    void Parse();
private:
    AddressBlock ParseAddressBlock(const pugi::xml_node& node);
    Register ParseRegister(const pugi::xml_node& node);
    RegisterField ParseRegField(const pugi::xml_node& node);
    Peripheral ParsePeripheral(const pugi::xml_node& node);
    Device ParseDevice(const pugi::xml_node& node);
    std::filesystem::path filePath_;
    pugi::xml_document svdDoc_;
    std::vector<Peripheral> derivedPeriphs_;
    Device device_;
};
}  // namespace providers::svd

#endif  // TRAILER_PROVIDERS_PERIPHERAL_PROVIDER_XML_PARSER_SVD_PARSER_HPP_
