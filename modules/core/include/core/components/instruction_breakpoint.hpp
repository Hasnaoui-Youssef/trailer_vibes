//===-- instruction_breakpoint.hpp ----------------------------------------===//
//
// Relocated from debug_service/instruction_breakpoint.hpp - see
// breakpoint_base.hpp.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_INSTRUCTION_BREAKPOINT_HPP_
#define TRAILER_CORE_COMPONENTS_INSTRUCTION_BREAKPOINT_HPP_

#include <cstdint>

#include "core/components/breakpoint.hpp"
#include "lldb/lldb-types.h"

namespace core {

/// Instruction Breakpoint
class InstructionBreakpoint : public Breakpoint {
public:
  InstructionBreakpoint(DebugContext &context,
                         const protocol::InstructionBreakpoint &breakpoint);

  /// Set instruction breakpoint in LLDB as a new breakpoint.
  void SetBreakpoint();

  lldb::addr_t GetInstructionAddressReference() const {
    return m_instruction_address_reference;
  }

protected:
  lldb::addr_t m_instruction_address_reference;
  int32_t m_offset;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_INSTRUCTION_BREAKPOINT_HPP_
