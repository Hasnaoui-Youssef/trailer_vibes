#include <concepts>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "svd_parser.hpp"
#include "svd_peripheral.hpp"
#include "svd_config.hpp"

namespace providers::svd {

template<typename T>
struct NodeTraits;

template<>
struct NodeTraits<std::string> {
    using type = std::string;
    static auto extract(const pugi::xml_node& f) { return std::string{f.text().as_string()}; }
};

template<>
struct NodeTraits<std::uint32_t> {
    using type = std::uint32_t;
    static auto extract(const pugi::xml_node& f) { return static_cast<std::uint32_t>(f.text().as_uint()); }
};

template<>
struct NodeTraits<bool> {
    using type = bool;
    static auto extract(const pugi::xml_node& f) { return f.text().as_bool(); }
};

template<typename T>
using NodeValueType = typename NodeTraits<T>::type;

template<typename Function, typename  T>
concept NodeCallback = std::invocable<Function, NodeValueType<T>>;

template<typename T>
auto extractNodeValue(const pugi::xml_node& node){
    return NodeTraits<T>::extract(node);
}

template<typename T, NodeCallback<T> Function>
bool SafeConsumeNode(const pugi::xml_node& node, Function&& callback)
//    -> std::optional<std::invoke_result<Function, nodeValueType<T>>>
{
        if(!node.text())
            return false;
        std::invoke(std::forward<Function>(callback), extractNodeValue<T>(node));
        return true;
}
template<typename T>
void SetToNodeValue(const pugi::xml_node& node, T& obj){
    SafeConsumeNode<T>(node, [&](T nodeValue){ obj = nodeValue; });
}
template<typename T>
void SetToOptionalNodeValue(const pugi::xml_node& node, std::optional<T>& obj){
    auto ret = SafeConsumeNode<T>(node, [&](T nodeValue){ *obj = nodeValue; });
    if(!ret){
        obj = std::nullopt;
    }
}

RegisterField SvdParser::ParseRegField(const pugi::xml_node& node){
    RegisterField rf{};
    SetToNodeValue(node.child(kNameNode), rf.name);
    SetToNodeValue(node.child(kDescriptionNode), rf.description);
    SetToNodeValue(node.child(kAccessTypeNode), rf.accessType.value);
    if(!rf.accessType.value.empty()){
        if (rf.accessType.value == "read-only") {
            rf.accessType.kind = AccessTypeKind::kReadOnly;
        } else if (rf.accessType.value == "write-only") {
            rf.accessType.kind = AccessTypeKind::kWriteOnly;
        } else if (rf.accessType.value == "read-write") {
            rf.accessType.kind = AccessTypeKind::kReadWrite;
        } else if (rf.accessType.value == "writeOnce") {
            rf.accessType.kind = AccessTypeKind::kWriteOnce;
        } else if (rf.accessType.value == "read-writeOnce") {
            rf.accessType.kind = AccessTypeKind::kReadWriteOnce;
        }
    }
    std::vector<EnumeratedValue> values;
    for(const auto& evNode : node.child(kEnumeratedValuesNode).children()){
        EnumeratedValue ev{};
        SetToNodeValue(evNode.child(kNameNode), ev.name);
        SetToNodeValue(evNode.child(kDescriptionNode), ev.description);
        SetToNodeValue(evNode.child(kValueNode), ev.value);
        values.push_back(ev);
    }
    if(!values.empty()){
        rf.enumerated_values = {values};
    } else {
        rf.enumerated_values = std::nullopt;
    }
    return rf;
}
Register SvdParser::ParseRegister(const pugi::xml_node& node){
    Register r{};
    SetToNodeValue(node.child(kNameNode), r.name);
    SetToNodeValue(node.child(kDescriptionNode), r.description);
    SetToOptionalNodeValue(node.child(kDisplayNameNode), r.displayName);
    SetToNodeValue(node.child(kAddressOffsetNode), r.addressOffset);
    SetToOptionalNodeValue(node.child(kResetValueNode), r.resetValue);
    SetToOptionalNodeValue(node.child(kResetMaskNode), r.resetMask);
    for(const auto& fieldNode : node.child(kFieldsNode).children()){
        r.fields.push_back(ParseRegField(fieldNode));
    }
    return r;
}

AddressBlock SvdParser::ParseAddressBlock(const pugi::xml_node& node){
    AddressBlock ab{};
    SetToNodeValue(node.child(kOffsetNode), ab.offset);
    SetToNodeValue(node.child(kSizeNode), ab.size);
    SetToNodeValue(node.child(kUsageNode), ab.usage);
    return ab;
}
Peripheral SvdParser::ParsePeripheral(const pugi::xml_node& node){
    Peripheral p{};
    SetToNodeValue(node.child(kNameNode), p.name);
    SetToNodeValue(node.child(kBaseAddressNode), p.baseAddress);
    if(std::string parentPeriph = std::string{node.attribute(kDerivedFromAttribute).value()}; !parentPeriph.empty()){
        for(const auto& ps : device_.peripherals){
            if(ps.name == parentPeriph){
                p.description = ps.description;
                p.groupName = ps.groupName;
                p.addressBlock = ps.addressBlock;
                p.registers = ps.registers;
            }
        }
    }
    SetToNodeValue(node.child(kDescriptionNode), p.description);
    SetToNodeValue(node.child(kGroupNameNode), p.groupName);
    p.addressBlock = ParseAddressBlock(node.child(kAddressBlockNode));
    for(const auto& regNode : node.child(kRegistersNode).children()){
        p.registers.push_back(ParseRegister(regNode));
    }
    return p;
}

static Cpu ParseCpu(const pugi::xml_node& node){
    Cpu cpu;
    SetToNodeValue(node.child(kEndianNode), cpu.endianess);
    SetToNodeValue(node.child(kFpuNode), cpu.fpuPresent);
    SetToNodeValue(node.child(kMpuNode), cpu.mpuPresent);
    SetToNodeValue(node.child(kNameNode), cpu.name);
    SetToNodeValue(node.child(kNVICPrioBitsNode), cpu.NVICPrioBits);
    SetToNodeValue(node.child(kRevisionNode), cpu.revision);
    SetToNodeValue(node.child(kVendorSystickConfigNode), cpu.vendorSystickConfig);
    return cpu;
}

Device SvdParser::ParseDevice(const pugi::xml_node& node){
    Device d{};
    SetToNodeValue(node.child(kNameNode), d.name);
    SetToNodeValue(node.child(kVersionNode), d.version);
    SetToNodeValue(node.child(kDescriptionNode), d.description);
    d.cpu = ParseCpu(node.child(kCpuNode));
    SetToNodeValue(node.child(kAddressUnitBitsNode), d.addressUnitBits);
    SetToNodeValue(node.child(kWidthNode), d.width);
    SetToNodeValue(node.child(kSizeNode), d.size);
    SetToNodeValue(node.child(kResetValueNode), d.resetValue);
    SetToNodeValue(node.child(kResetMaskNode), d.resetMask);
    for(const auto& periphNode : node.child(kPeriphsNode).children()){
        d.peripherals.push_back(ParsePeripheral(periphNode));
    }
    return d;
}


void SvdParser::Parse() {
    pugi::xml_parse_result res = svdDoc_.load_file(filePath_.c_str());
    if (res.status != pugi::xml_parse_status::status_ok) {
        return;
    }
    device_ = ParseDevice(svdDoc_.child(kDeviceNode));
}
}