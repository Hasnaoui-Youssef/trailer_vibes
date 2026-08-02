#include "device_xml/svd_loader.hpp"

#include <cstdlib>
#include <memory>
#include <unordered_map>

#include "../generated/CMSIS_SVD.hxx"

namespace device_xml {

namespace {

std::uint32_t ParseScaledInt(const std::string &text) {
    if (text.empty()) return 0;
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    int base = 10;
    if (text[i] == '#') {
        base = 2;
        ++i;
    } else if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        base = 16;
        i += 2;
    }
    char *end = nullptr;
    unsigned long value = std::strtoul(text.c_str() + i, &end, base);
    if (end != nullptr) {
        switch (*end) {
            case 'k':
            case 'K':
                value *= 1024;
                break;
            case 'm':
            case 'M':
                value *= 1024 * 1024;
                break;
            case 'g':
            case 'G':
                value *= 1024 * 1024 * 1024;
                break;
            default:
                break;
        }
    }
    return static_cast<std::uint32_t>(value);
}

AccessKind ToAccessKind(const ::accessType &access) {
    switch (static_cast<::accessType::value>(access)) {
        case ::accessType::read_only:
            return AccessKind::kReadOnly;
        case ::accessType::write_only:
            return AccessKind::kWriteOnly;
        case ::accessType::read_write:
            return AccessKind::kReadWrite;
        case ::accessType::writeOnce:
            return AccessKind::kWriteOnce;
        case ::accessType::read_writeOnce:
            return AccessKind::kReadWriteOnce;
    }
    return AccessKind::kUnspecified;
}

ReadActionKind ToReadActionKind(const ::readActionType &action) {
    switch (static_cast<::readActionType::value>(action)) {
        case ::readActionType::clear:
            return ReadActionKind::kClear;
        case ::readActionType::set:
            return ReadActionKind::kSet;
        case ::readActionType::modify:
            return ReadActionKind::kModify;
        case ::readActionType::modifyExternal:
            return ReadActionKind::kModifyExternal;
    }
    return ReadActionKind::kNone;
}

struct BitRange {
    std::uint32_t offset;
    std::uint32_t width;
};

BitRange ResolveBitRange(const ::fieldType &field) {
    if (field.bitOffset()) {
        std::uint32_t offset = ParseScaledInt(static_cast<std::string>(*field.bitOffset()));
        std::uint32_t width = field.bitWidth() ? ParseScaledInt(static_cast<std::string>(*field.bitWidth())) : 1;
        return {offset, width};
    }
    if (field.lsb() && field.msb()) {
        std::uint32_t lsb = ParseScaledInt(static_cast<std::string>(*field.lsb()));
        std::uint32_t msb = ParseScaledInt(static_cast<std::string>(*field.msb()));
        return {lsb, msb >= lsb ? msb - lsb + 1 : 1};
    }
    if (field.bitRange()) {
        const std::string text = static_cast<std::string>(*field.bitRange());
        const auto colon = text.find(':');
        const auto open = text.find('[');
        const auto close = text.find(']');
        if (colon != std::string::npos && open != std::string::npos && close != std::string::npos) {
            const std::uint32_t msb = ParseScaledInt(text.substr(open + 1, colon - open - 1));
            const std::uint32_t lsb = ParseScaledInt(text.substr(colon + 1, close - colon - 1));
            return {lsb, msb >= lsb ? msb - lsb + 1 : 1};
        }
    }
    return {0, 0};
}

std::vector<EnumeratedValue> TranslateEnumeratedValues(const ::fieldType &field) {
    std::vector<EnumeratedValue> values;
    for (const ::enumerationType &group : field.enumeratedValues()) {
        for (const ::enumeratedValueType &ev : group.enumeratedValue()) {
            EnumeratedValue value;
            value.name = ev.name();
            if (ev.description()) value.description = *ev.description();
            if (ev.value()) value.value = ParseScaledInt(static_cast<std::string>(*ev.value()));
            if (ev.isDefault()) value.is_default = *ev.isDefault();
            values.push_back(std::move(value));
        }
    }
    return values;
}

RegisterField TranslateField(const ::fieldType &field, AccessKind inherited_access,
                              ReadActionKind inherited_read_action) {
    RegisterField out;
    out.name = field.name();
    if (field.description()) out.description = *field.description();
    const BitRange range = ResolveBitRange(field);
    out.bit_offset = range.offset;
    out.bit_width = range.width;
    out.access = field.access() ? ToAccessKind(*field.access()) : inherited_access;
    out.read_action = field.readAction() ? ToReadActionKind(*field.readAction()) : inherited_read_action;
    out.enumerated_values = TranslateEnumeratedValues(field);
    return out;
}

AccessKind AggregateAccess(const std::vector<RegisterField> &fields, AccessKind fallback) {
    if (fields.empty()) return fallback;
    bool all_write_only = true;
    bool all_read_only = true;
    for (const RegisterField &field : fields) {
        if (field.access != AccessKind::kWriteOnly) all_write_only = false;
        if (field.access != AccessKind::kReadOnly) all_read_only = false;
    }
    if (all_write_only) return AccessKind::kWriteOnly;
    if (all_read_only) return AccessKind::kReadOnly;
    return AccessKind::kReadWrite;
}

ReadActionKind AggregateReadAction(const std::vector<RegisterField> &fields) {
    for (const RegisterField &field : fields) {
        if (field.read_action != ReadActionKind::kNone) return field.read_action;
    }
    return ReadActionKind::kNone;
}

Register TranslateRegister(const ::registerType &reg, std::uint32_t base_offset, AccessKind inherited_access,
                            ReadActionKind inherited_read_action) {
    Register out;
    out.name = reg.name();
    if (reg.description()) out.description = *reg.description();
    out.address_offset = base_offset + ParseScaledInt(static_cast<std::string>(reg.addressOffset()));
    out.size_bits = reg.size() ? ParseScaledInt(static_cast<std::string>(*reg.size())) : 32;

    const AccessKind declared_access = reg.access() ? ToAccessKind(*reg.access()) : AccessKind::kUnspecified;
    const AccessKind field_default = declared_access != AccessKind::kUnspecified ? declared_access : inherited_access;
    const ReadActionKind declared_read_action =
        reg.readAction() ? ToReadActionKind(*reg.readAction()) : ReadActionKind::kNone;

    if (reg.fields()) {
        for (const ::fieldType &field : reg.fields()->field()) {
            out.fields.push_back(TranslateField(field, field_default, declared_read_action));
        }
    }

    out.access = declared_access != AccessKind::kUnspecified ? declared_access : AggregateAccess(out.fields, field_default);
    out.read_action = declared_read_action != ReadActionKind::kNone ? declared_read_action : AggregateReadAction(out.fields);

    if (reg.resetValue()) out.reset_value = ParseScaledInt(static_cast<std::string>(*reg.resetValue()));
    if (reg.resetMask()) out.reset_mask = ParseScaledInt(static_cast<std::string>(*reg.resetMask()));

    return out;
}

void FlattenCluster(const ::clusterType &cluster, std::uint32_t base_offset, const std::string &name_prefix,
                     AccessKind inherited_access, ReadActionKind inherited_read_action,
                     std::vector<Register> &out) {
    const std::uint32_t offset = base_offset + ParseScaledInt(static_cast<std::string>(cluster.addressOffset()));
    const std::string prefix = name_prefix + static_cast<std::string>(cluster.name()) + "_";
    const AccessKind access = cluster.access() ? ToAccessKind(*cluster.access()) : inherited_access;

    for (const ::registerType &reg : cluster.register_()) {
        Register translated = TranslateRegister(reg, offset, access, inherited_read_action);
        translated.name = prefix + translated.name;
        out.push_back(std::move(translated));
    }
    for (const ::clusterType &nested : cluster.cluster()) {
        FlattenCluster(nested, offset, prefix, access, inherited_read_action, out);
    }
}

AddressBlock TranslateAddressBlock(const ::addressBlockType &block) {
    AddressBlock out;
    out.offset = ParseScaledInt(static_cast<std::string>(block.offset()));
    out.size = ParseScaledInt(static_cast<std::string>(block.size()));
    out.usage = block.usage();
    return out;
}

Interrupt TranslateInterrupt(const ::interruptType &interrupt) {
    Interrupt out;
    out.name = interrupt.name();
    if (interrupt.description()) out.description = *interrupt.description();
    out.value = static_cast<std::int32_t>(interrupt.value());
    return out;
}

class PeripheralTranslator {
public:
    explicit PeripheralTranslator(AccessKind device_access) : m_device_access(device_access) {}

    void IndexPeripherals(const ::peripherals::peripheral_sequence &peripherals) {
        for (const ::peripheralType &p : peripherals) {
            m_by_name[p.name()] = &p;
        }
    }

    Peripheral Translate(const ::peripheralType &node) { return TranslateImpl(node, 0); }

private:
    Peripheral TranslateImpl(const ::peripheralType &node, int depth) {
        Peripheral out;
        if (node.derivedFrom() && depth < 8) {
            const auto it = m_by_name.find(static_cast<std::string>(*node.derivedFrom()));
            if (it != m_by_name.end()) {
                out = TranslateImpl(*it->second, depth + 1);
            }
        }

        out.name = node.name();
        out.base_address = ParseScaledInt(static_cast<std::string>(node.baseAddress()));
        if (node.description()) out.description = *node.description();
        if (node.groupName()) out.group_name = *node.groupName();

        const AccessKind access = node.access() ? ToAccessKind(*node.access()) : m_device_access;

        if (!node.addressBlock().empty()) {
            out.address_block = TranslateAddressBlock(node.addressBlock().front());
        }

        if (node.registers()) {
            out.registers.clear();
            for (const ::registerType &reg : node.registers()->register_()) {
                out.registers.push_back(TranslateRegister(reg, 0, access, ReadActionKind::kNone));
            }
            for (const ::clusterType &cluster : node.registers()->cluster()) {
                FlattenCluster(cluster, 0, "", access, ReadActionKind::kNone, out.registers);
            }
        }

        if (!node.interrupt().empty()) {
            out.interrupts.clear();
            for (const ::interruptType &interrupt : node.interrupt()) {
                out.interrupts.push_back(TranslateInterrupt(interrupt));
            }
        }

        return out;
    }

    AccessKind m_device_access;
    std::unordered_map<std::string, const ::peripheralType *> m_by_name;
};

}  // namespace

std::expected<Device, std::string> LoadSvd(const std::filesystem::path &path) {
    try {
        std::unique_ptr<::device> root =
            device_ (path.string(), ::xml_schema::flags::dont_validate);

        Device out;
        out.name = root->name();
        out.version = root->version();
        out.description = root->description();
        out.address_unit_bits = ParseScaledInt(static_cast<std::string>(root->addressUnitBits()));
        out.width = ParseScaledInt(static_cast<std::string>(root->width()));

        if (root->cpu()) {
            const ::cpuType &cpu_node = *root->cpu();
            Cpu cpu;
            cpu.name = static_cast<std::string>(cpu_node.name());
            cpu.revision = cpu_node.revision();
            cpu.endian = static_cast<std::string>(cpu_node.endian());
            cpu.mpu_present = cpu_node.mpuPresent() ? bool(*cpu_node.mpuPresent()) : false;
            cpu.fpu_present = cpu_node.fpuPresent() ? bool(*cpu_node.fpuPresent()) : false;
            cpu.nvic_prio_bits = ParseScaledInt(static_cast<std::string>(cpu_node.nvicPrioBits()));
            cpu.vendor_systick_config = bool(cpu_node.vendorSystickConfig());
            out.cpu = std::move(cpu);
        }

        const AccessKind device_access = root->access() ? ToAccessKind(*root->access()) : AccessKind::kUnspecified;

        PeripheralTranslator translator(device_access);
        translator.IndexPeripherals(root->peripherals().peripheral());
        for (const ::peripheralType &p : root->peripherals().peripheral()) {
            out.peripherals.push_back(translator.Translate(p));
        }

        return out;
    } catch (const ::xml_schema::exception &e) {
        std::ostringstream oss;
        oss << e;
        return std::unexpected(oss.str());
    } catch (const std::exception &e) {
        return std::unexpected(std::string(e.what()));
    }
}

}  // namespace device_xml
