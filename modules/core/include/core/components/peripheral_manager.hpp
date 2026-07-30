#ifndef TRAILER_CORE_COMPONENTS_PERIPHERAL_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_PERIPHERAL_MANAGER_HPP_
#include "core/debug_context.hpp"
namespace core {
class PeripheralManager {
public:
    explicit PeripheralManager(DebugContext& context) : m_context(context){}
    // Peripheral GetPeripheral()
    // SetRegisterValue(const std::string& peripheralName, const std::string& registerName, std::uint32_t value)
private:
    DebugContext& m_context;
};
}
#endif  // TRAILER_CORE_COMPONENTS_PERIPHERAL_MANAGER_HPP_
