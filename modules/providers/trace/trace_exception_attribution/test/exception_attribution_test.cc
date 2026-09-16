// Deterministic checks for ExceptionName/AttributeExceptions: a synthetic
// Device stands in for a real SVD load, so this needs no hardware and no
// SVD file on disk.

#include "trace_exception_attribution/exception_attribution.hpp"

#include <cstdlib>
#include <iostream>

namespace {

device_xml::Device MakeTestDevice() {
    device_xml::Device device;
    device_xml::Peripheral tim6;
    tim6.name = "TIM6";
    tim6.interrupts.push_back(device_xml::Interrupt{.name = "TIM6", .description = "TIM6 global interrupt", .value = 55});
    device.peripherals.push_back(tim6);
    return device;
}

void Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

}  // namespace

int main() {
    const device_xml::Device device = MakeTestDevice();

    Check(trace::ExceptionName(1, device) == "Reset", "exception 1 is Reset");
    Check(trace::ExceptionName(3, device) == "HardFault", "exception 3 is HardFault");
    Check(trace::ExceptionName(15, device) == "SysTick", "exception 15 is SysTick");
    Check(trace::ExceptionName(7, device) == "Reserved", "exception 7 is a reserved core slot");

    Check(trace::ExceptionName(16, device) == "IRQ0 (unnamed)", "an IRQ no peripheral declares reports unnamed, not silently dropped");
    Check(trace::ExceptionName(24, device) == "DebugHalt", "exception 24 is the M-class DebugHalt pseudo-exception");
    Check(trace::ExceptionName(25, device) == "LazyFP Push", "exception 25 is LazyFP Push");
    Check(trace::ExceptionName(26, device) == "Lockup", "exception 26 is Lockup");

    // TIM6's SVD interrupt value is 55; IRQ8 and above are encoded at +0x200.
    Check(trace::ExceptionName(0x200 + 55, device) == "TIM6", "exception 567 (0x200+55, IRQ55) resolves to TIM6 via the SVD model");

    const std::vector<model::ExceptionEvent> events = {
        {.instruction_index = 10, .exception_number = 0x200 + 55, .is_return = false},
        {.instruction_index = 42, .exception_number = 0, .is_return = true},
    };
    const std::vector<trace::AttributedException> attributed = trace::AttributeExceptions(events, device);
    Check(attributed.size() == 2, "one AttributedException per ExceptionEvent");
    Check(attributed[0].name == "TIM6", "entry event resolves its name");
    Check(attributed[0].instruction_index == 10, "entry event keeps its instruction index");
    Check(attributed[1].is_return, "return event is marked as a return");
    Check(attributed[1].name.empty(), "return event's exception_number (0) is never resolved to a misleading name");

    std::cout << "exception_attribution_test: OK\n";
    return 0;
}
