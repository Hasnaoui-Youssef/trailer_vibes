#include "trace_exception_attribution/exception_attribution.hpp"

#include <array>
#include <format>
#include <optional>

namespace trace {

namespace {

// ETMv4's own M-class exception numbering, the wire encoding OpenCSD
// copies verbatim into TraceRecord::exception_number (see
// trc_pkt_elem_etmv4i.cpp's own "MExcep" table).
constexpr std::array<const char *, 32> kMClassExceptionNames = {
    "Reserved", "Reset", "NMI", "HardFault",             // 0-3
    "MemManage", "BusFault", "UsageFault", "Reserved",   // 4-7
    "Reserved", "Reserved", "Reserved", "SVCall",        // 8-11
    "DebugMonitor", "Reserved", "PendSV", "SysTick",     // 12-15
    "IRQ0", "IRQ1", "IRQ2", "IRQ3",                      // 16-19
    "IRQ4", "IRQ5", "IRQ6", "IRQ7",                      // 20-23
    "DebugHalt", "LazyFP Push", "Lockup", "Reserved",    // 24-27
    "Reserved", "Reserved", "Reserved", "Reserved",      // 28-31
};
constexpr uint32_t kIrq8PlusBase = 0x208;
constexpr uint32_t kIrq8PlusLimit = 0x3EF;

std::optional<std::string> FindPeripheralInterruptName(uint32_t irq_number, const device_xml::Device &device) {
    for (const device_xml::Peripheral &peripheral : device.peripherals) {
        for (const device_xml::Interrupt &interrupt : peripheral.interrupts) {
            if (interrupt.value == static_cast<std::int32_t>(irq_number)) {
                return interrupt.name;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> NamedPeripheralIrq(uint32_t exception_number, const device_xml::Device &device) {
    uint32_t irq_number;
    if (exception_number >= 16 && exception_number <= 23) {
        irq_number = exception_number - 16;  // IRQ0-IRQ7
    } else if (exception_number >= kIrq8PlusBase && exception_number <= kIrq8PlusLimit) {
        irq_number = exception_number - 0x200;  // IRQ8 and above
    } else {
        return std::nullopt;
    }
    if (const std::optional<std::string> name = FindPeripheralInterruptName(irq_number, device)) {
        return *name;
    }
    return std::format("IRQ{} (unnamed)", irq_number);
}

}  // namespace

std::string ExceptionName(uint32_t exception_number, const device_xml::Device &device) {
    if (const std::optional<std::string> irq_name = NamedPeripheralIrq(exception_number, device)) {
        return *irq_name;
    }
    if (exception_number < kMClassExceptionNames.size()) {
        return kMClassExceptionNames[exception_number];
    }
    return std::format("Exception{} (reserved)", exception_number);
}

std::vector<AttributedException> AttributeExceptions(std::span<const model::ExceptionEvent> events,
                                                       const device_xml::Device &device) {
    std::vector<AttributedException> attributed;
    attributed.reserve(events.size());
    for (const model::ExceptionEvent &event : events) {
        attributed.push_back(AttributedException{
            .instruction_index = event.instruction_index,
            .exception_number = event.exception_number,
            .name = event.is_return ? std::string() : ExceptionName(event.exception_number, device),
            .is_return = event.is_return,
        });
    }
    return attributed;
}

}  // namespace trace
