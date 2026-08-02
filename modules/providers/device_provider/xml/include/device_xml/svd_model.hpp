#ifndef TRAILER_PROVIDERS_DEVICE_PROVIDER_XML_SVD_MODEL_HPP_
#define TRAILER_PROVIDERS_DEVICE_PROVIDER_XML_SVD_MODEL_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace device_xml {

enum class AccessKind {
    kUnspecified,
    kReadOnly,
    kWriteOnly,
    kReadWrite,
    kWriteOnce,
    kReadWriteOnce,
};

enum class ReadActionKind {
    kNone,
    kClear,
    kSet,
    kModify,
    kModifyExternal,
};

struct EnumeratedValue {
    std::string name;
    std::string description;
    std::optional<std::uint32_t> value;
    bool is_default = false;
};

struct RegisterField {
    std::string name;
    std::string description;
    std::uint32_t bit_offset = 0;
    std::uint32_t bit_width = 0;
    AccessKind access = AccessKind::kUnspecified;
    ReadActionKind read_action = ReadActionKind::kNone;
    std::vector<EnumeratedValue> enumerated_values;
};

struct AddressBlock {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::string usage;
};

struct Register {
    std::string name;
    std::string description;
    std::uint32_t address_offset = 0;
    std::uint32_t size_bits = 32;
    AccessKind access = AccessKind::kUnspecified;
    ReadActionKind read_action = ReadActionKind::kNone;
    std::optional<std::uint32_t> reset_value;
    std::optional<std::uint32_t> reset_mask;
    std::vector<RegisterField> fields;

    bool ReadSafe() const {
        return read_action == ReadActionKind::kNone && access != AccessKind::kWriteOnly;
    }
};

struct Interrupt {
    std::string name;
    std::string description;
    std::int32_t value = 0;
};

struct Peripheral {
    std::string name;
    std::string description;
    std::string group_name;
    std::uint32_t base_address = 0;
    AddressBlock address_block;
    std::vector<Register> registers;
    std::vector<Interrupt> interrupts;
};

struct Cpu {
    std::string name;
    std::string revision;
    std::string endian;
    bool mpu_present = false;
    bool fpu_present = false;
    std::uint32_t nvic_prio_bits = 0;
    bool vendor_systick_config = false;
};

struct Device {
    std::string name;
    std::string version;
    std::string description;
    std::optional<Cpu> cpu;
    std::uint32_t address_unit_bits = 8;
    std::uint32_t width = 32;
    std::vector<Peripheral> peripherals;
};

}  // namespace device_xml

#endif  // TRAILER_PROVIDERS_DEVICE_PROVIDER_XML_SVD_MODEL_HPP_
