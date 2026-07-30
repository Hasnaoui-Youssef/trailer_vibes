#ifndef TRAILER_PROVIDERS_PERIPHERAL_PROVIDER_XML_PARSER_SVD_CONFIG_HPP_
#define TRAILER_PROVIDERS_PERIPHERAL_PROVIDER_XML_PARSER_SVD_CONFIG_HPP_
namespace providers::svd {
static const char* kDeviceNode = "device";
static const char* kNameNode = "name";
static const char* kDescriptionNode = "description";
static const char* kVersionNode = "version";
static const char* kCpuNode = "cpu";
static const char* kRevisionNode = "revision";
static const char* kEndianNode = "endian";
static const char* kMpuNode = "mpuPresent";
static const char* kFpuNode = "fpuPresent";
static const char* kNVICPrioBitsNode = "nvicPrioBits";
static const char* kVendorSystickConfigNode = "vendorSystickConfig";
static const char* kAddressUnitBitsNode = "addressUnitBits";
static const char* kWidthNode = "width";
static const char* kSizeNode = "size";
static const char* kResetValueNode = "resetValue";
static const char* kResetMaskNode = "resetMask";
static const char* kPeriphsNode = "peripherals";
static const char* kPeriphNode = "peripheral";
static const char* kGroupNameNode = "groupName";
static const char* kBaseAddressNode = "baseAddress";
static const char* kAddressBlockNode = "addressBlock";
static const char* kOffsetNode = "offset";
static const char* kUsageNode = "usage";
static const char* kRegistersNode = "registers";
static const char* kRegisterNode = "register";
static const char* kDisplayNameNode = "displayName";
static const char* kAddressOffsetNode = "addressOffset";
static const char* kFieldsNode = "fields";
static const char* kFieldNode = "field";
static const char* kBitOffsetNode = "bitOffset";
static const char* kBitWidthNode = "bitWidth";
static const char* kAccessTypeNode = "access";
static const char* kEnumeratedValuesNode = "enumeratedValues";
static const char* kEnumeratedValueNode = "enumeratedValue";
static const char* kValueNode = "value";
static const char* kInterruptNode = "interrupt";

static const char* kDerivedFromAttribute = "derivedFrom";
}
#endif  // TRAILER_PROVIDERS_PERIPHERAL_PROVIDER_XML_PARSER_SVD_CONFIG_HPP_
