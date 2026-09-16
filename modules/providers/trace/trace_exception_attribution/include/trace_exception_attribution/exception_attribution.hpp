#ifndef TRAILER_TRACE_EXCEPTION_ATTRIBUTION_EXCEPTION_ATTRIBUTION_HPP_
#define TRAILER_TRACE_EXCEPTION_ATTRIBUTION_EXCEPTION_ATTRIBUTION_HPP_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "device_xml/svd_model.hpp"
#include "trace_model/exception_event.hpp"

namespace trace {

// Names an ETMv4 M-class exception number: core exceptions and pseudo-
// exceptions (Reset, HardFault, ... SysTick, DebugHalt, Lockup, ...) are
// fixed and architecturally the same on every Cortex-M part; peripheral
// IRQs are named by matching their number against a peripheral's
// <interrupt> value in the device's own SVD. Returns "IRQ<n> (unnamed)"
// for an IRQ no peripheral in this device declares, and "Exception<n>
// (reserved)" for a reserved core slot - never silently drops a number
// this pipeline observed.
std::string ExceptionName(uint32_t exception_number, const device_xml::Device &device);

// An ExceptionEvent with its exception_number resolved to a name, ready
// for reporting. is_return events have exception_number == 0 (matching
// ExceptionEvent's own doc comment) and get an empty name - resolving it
// would misleadingly report exception number 0's name on a return.
struct AttributedException {
    uint64_t instruction_index;
    uint32_t exception_number;
    std::string name;
    bool is_return;
};

std::vector<AttributedException> AttributeExceptions(std::span<const model::ExceptionEvent> events,
                                                       const device_xml::Device &device);

}  // namespace trace

#endif  // TRAILER_TRACE_EXCEPTION_ATTRIBUTION_EXCEPTION_ATTRIBUTION_HPP_
