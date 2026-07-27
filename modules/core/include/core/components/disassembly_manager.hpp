//===-- disassembly_manager.hpp -------------------------------------------===//
//
// The disassembly component (see CLAUDE.md's DebugContext architecture).
// Carved out of the disassemble request handler's inline logic - there was
// no DebugService::disassemble method to move (see PROJECT_STATUS.md).
// Still SB-backed (target.ReadInstructions) for behavior parity, same as
// before the carve; wiring to the providers/disassembler module is future
// work (see the plan's non-goals).
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_DISASSEMBLY_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_DISASSEMBLY_MANAGER_HPP_

#include <cstdint>
#include <vector>

#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/lldb-types.h"
#include "llvm/Support/Error.h"

namespace core {

class DebugContext;

class DisassemblyManager {
public:
  explicit DisassemblyManager(DebugContext &context) : m_context(context) {}

  static constexpr uint32_t k_number_of_assembly_lines_for_nodebug = 32;

  llvm::Expected<std::vector<dap::protocol::DisassembledInstruction>>
  Disassemble(lldb::addr_t memory_reference, int64_t byte_offset, int64_t instruction_offset,
              uint64_t instruction_count, bool resolve_symbols);

  llvm::Expected<dap::protocol::SourceResponseBody> GetSourceRequest(
      const dap::protocol::SourceArguments &args);

private:
  DebugContext &m_context;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_DISASSEMBLY_MANAGER_HPP_
