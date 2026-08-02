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
#include <expected>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "disassembler/program_disassembler.hpp"
#include "lldb/lldb-types.h"
#include "llvm/Support/Error.h"

namespace core {

class DebugContext;

class DisassemblyManager {
public:
  explicit DisassemblyManager(DebugContext &context) : m_context(context) {}
  ~DisassemblyManager();

  static constexpr uint32_t k_number_of_assembly_lines_for_nodebug = 32;

  llvm::Expected<std::vector<dap::protocol::DisassembledInstruction>>
  Disassemble(lldb::addr_t memory_reference, int64_t byte_offset, int64_t instruction_offset,
              uint64_t instruction_count, bool resolve_symbols);

  llvm::Expected<dap::protocol::SourceResponseBody> GetSourceRequest(
      const dap::protocol::SourceArguments &args);

  // Waits for the whole-image LLVM/DWARF precompute that InvalidateProgram()
  // starts on a dedicated worker thread, then returns the cached result.
  // Called from the dispatch thread and from the trace-decode worker thread,
  // so access to the shared future is guarded by m_mutex - the future's own
  // value access (get()) is safe to call concurrently on its own.
  llvm::Expected<const disasm::ProgramDisassembler &> Program();

  // (Re)starts the precompute worker for the session's current program path.
  // Must be called whenever the program changes (or first becomes known) -
  // Program() never computes anything itself, only waits.
  void InvalidateProgram();

private:
  DebugContext &m_context;
  std::mutex m_mutex;
  std::thread program_worker_;
  std::shared_future<std::expected<disasm::ProgramDisassembler, std::string>> program_future_;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_DISASSEMBLY_MANAGER_HPP_
