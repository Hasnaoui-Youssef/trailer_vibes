#ifndef TRAILER_PROVIDERS_PERIPHERAL_PROVIDER_XML_PARSER_SVD_PERIPHERAL_HPP_
#define TRAILER_PROVIDERS_PERIPHERAL_PROVIDER_XML_PARSER_SVD_PERIPHERAL_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace providers::svd {

enum AccessTypeKind : unsigned int {
    kReadOnly,
    kWriteOnly,
    kReadWrite,
    kWriteOnce,
    kReadWriteOnce,
};
struct AccessType {
    enum AccessTypeKind kind;
    std::string value;
};
struct EnumeratedValue {
    std::string name;
    std::string description;
    std::uint32_t value;
};
struct RegisterField {
    std::string name;
    std::string description;
    std::uint32_t bitOffset;
    std::uint32_t bitWidth;
    AccessType accessType;
    std::optional<std::vector<EnumeratedValue>> enumerated_values;
};
struct Register {
    std::string name;
    std::string description;
    std::optional<std::string> displayName;
    std::uint32_t addressOffset;
    std::optional<std::uint32_t> resetValue;
    std::optional<std::uint32_t> resetMask;
    std::vector<RegisterField> fields;
};
struct AddressBlock {
    std::uint32_t offset;
    std::uint32_t size;
    std::string usage;
};
struct Peripheral {
    std::string name;
    std::string description;
    std::string groupName;
    std::uint32_t baseAddress;
    AddressBlock addressBlock;
    std::vector<Register> registers;
};

struct Cpu {
    std::string name;
    std::string revision;
    std::string endianess;
    bool mpuPresent;
    bool fpuPresent;
    std::uint32_t NVICPrioBits;
    bool vendorSystickConfig;
};
struct Device {
    std::string name;
    std::string version;
    std::string description;
    Cpu cpu;
    std::uint32_t addressUnitBits;
    std::uint32_t width;
    std::uint32_t size;
    std::uint32_t resetValue;
    std::uint32_t resetMask;
    std::vector<Peripheral> peripherals;
};

}  // namespace providers::svd

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_MEMORY_SELECTOR_HPP_