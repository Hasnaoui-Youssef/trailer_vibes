#include "core/components/instruction_breakpoint.hpp"

#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBTarget.h"
#include "llvm/ADT/StringRef.h"

namespace core {

InstructionBreakpoint::InstructionBreakpoint(DebugContext &context,
                                              const protocol::InstructionBreakpoint &breakpoint)
    : Breakpoint(context, breakpoint.condition, breakpoint.hitCondition),
      m_instruction_address_reference(LLDB_INVALID_ADDRESS),
      m_offset(breakpoint.offset.value_or(0)) {
  llvm::StringRef instruction_reference(breakpoint.instructionReference);
  instruction_reference.getAsInteger(0, m_instruction_address_reference);
  m_instruction_address_reference += m_offset;
}

void InstructionBreakpoint::SetBreakpoint() {
  m_bp = m_context.Target().BreakpointCreateByAddress(m_instruction_address_reference);
  Breakpoint::SetBreakpoint();
}

}  // namespace core
