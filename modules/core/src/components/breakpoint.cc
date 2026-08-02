#include "core/components/breakpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/components/device_manager.hpp"
#include "core/components/disassembly_manager.hpp"
#include "core/components/target_manager.hpp"
#include "core/lldb_utils.hpp"
#include "device_provider/memory_map.hpp"
#include "disassembler/program_disassembler.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBBreakpointLocation.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBMutex.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Error.h"

namespace core {

namespace {

std::optional<protocol::PersistenceData> GetPersistenceDataForSymbol(lldb::SBSymbol &symbol) {
  protocol::PersistenceData persistence_data;
  lldb::SBModule module = symbol.GetStartAddress().GetModule();
  if (!module.IsValid())
    return std::nullopt;

  lldb::SBFileSpec file_spec = module.GetFileSpec();
  if (!file_spec.IsValid())
    return std::nullopt;

  persistence_data.module_path = GetSBFileSpecPath(file_spec);
  persistence_data.symbol_name = symbol.GetName();
  return persistence_data;
}

bool RequiresHardware(lldb::SBBreakpoint &bp, const providers::device::MemoryMap &memory) {
  if (memory.Empty())
    return true;

  const auto num_locs = bp.GetNumLocations();
  for (size_t i = 0; i < num_locs; ++i) {
    lldb::SBBreakpointLocation loc = bp.GetLocationAtIndex(i);
    const lldb::addr_t addr = loc.GetLoadAddress();
    if (addr == LLDB_INVALID_ADDRESS || !memory.IsRam(addr))
      return true;
  }
  return false;
}

}  // namespace

void Breakpoint::SetCondition() { m_bp.SetCondition(m_condition.c_str()); }

void Breakpoint::SetHitCondition() {
  uint64_t hitCount = 0;
  if (llvm::to_integer(m_hit_condition, hitCount))
    m_bp.SetIgnoreCount(hitCount - 1);
}

protocol::Breakpoint Breakpoint::ToProtocolBreakpoint() {
  protocol::Breakpoint breakpoint;

  // Each breakpoint location is treated as a separate breakpoint for VS code.
  // They don't have the notion of a single breakpoint with multiple locations.
  if (!m_bp.IsValid())
    return breakpoint;

  breakpoint.id = m_bp.GetID();
  if (!m_hardware_error.empty())
    breakpoint.message = m_hardware_error;

  // Prefer an enabled+resolved location - PruneNonCodeLocations() disables
  // ones that aren't real code, so a disabled one isn't a real site.
  lldb::SBBreakpointLocation best;
  lldb::SBBreakpointLocation enabled;
  const auto num_locs = m_bp.GetNumLocations();
  for (size_t i = 0; i < num_locs && !best.IsValid(); ++i) {
    lldb::SBBreakpointLocation loc = m_bp.GetLocationAtIndex(i);
    if (!loc.IsEnabled())
      continue;
    if (!enabled.IsValid())
      enabled = loc;
    if (loc.IsResolved())
      best = loc;
  }
  lldb::SBBreakpointLocation bp_loc = best.IsValid() ? best : (enabled.IsValid() ? enabled : m_bp.GetLocationAtIndex(0));
  breakpoint.verified = best.IsValid();
  auto bp_addr = bp_loc.GetAddress();

  if (bp_addr.IsValid()) {
    std::string formatted_addr =
        "0x" + llvm::utohexstr(bp_addr.GetLoadAddress(m_bp.GetTarget()));
    breakpoint.instructionReference = formatted_addr;

    std::optional<protocol::Source> source = m_context.ResolveSource(bp_addr);
    const bool is_assembly_source = source && source->sourceReference.value_or(0) != 0;
    if (source && !is_assembly_source) {
      const CallSiteLocation call_site = GetOutermostInlinedCallSite(bp_addr);
      if (call_site.file.IsValid()) {
        // ResolveSource() above hits the same innermost-row resolution, so
        // source needs overriding here too.
        if (std::optional<protocol::Source> call_site_source = CreateSource(call_site.file))
          source = std::move(call_site_source);
        if (call_site.line != LLDB_INVALID_LINE_NUMBER)
          breakpoint.line = call_site.line;
        if (call_site.column != LLDB_INVALID_COLUMN_NUMBER)
          breakpoint.column = call_site.column;
      } else {
        auto line_entry = bp_addr.GetLineEntry();
        const auto line = line_entry.GetLine();
        if (line != LLDB_INVALID_LINE_NUMBER)
          breakpoint.line = line;
        const auto column = line_entry.GetColumn();
        if (column != LLDB_INVALID_COLUMN_NUMBER)
          breakpoint.column = column;
      }
    } else if (source) {
      // Assembly breakpoint.
      auto symbol = bp_addr.GetSymbol();
      if (symbol.IsValid()) {
        breakpoint.line =
            m_bp.GetTarget()
                .ReadInstructions(symbol.GetStartAddress(), bp_addr, nullptr)
                .GetSize() +
            1;

        std::optional<protocol::PersistenceData> persistence_data =
            GetPersistenceDataForSymbol(symbol);
        if (persistence_data) {
          source->adapterData =
              protocol::SourceLLDBData{std::move(persistence_data)};
        }
      }
    }

    breakpoint.source = std::move(source);
  }

  return breakpoint;
}

bool Breakpoint::MatchesName(const char *name) {
  return m_bp.MatchesName(name);
}

void Breakpoint::PruneNonCodeLocations() {
  llvm::Expected<const disasm::ProgramDisassembler &> program = m_context.Disassembly().Program();
  if (!program) {
    llvm::consumeError(program.takeError());
    return;
  }

  const auto num_locs = m_bp.GetNumLocations();
  for (size_t i = 0; i < num_locs; ++i) {
    lldb::SBBreakpointLocation loc = m_bp.GetLocationAtIndex(i);
    const lldb::addr_t addr = loc.GetLoadAddress();
    if (addr != LLDB_INVALID_ADDRESS && program->InstructionInfoAt(addr) == nullptr)
      loc.SetEnabled(false);
  }
}

void Breakpoint::SetBreakpoint() {
  m_context.WithTarget([&]() {
    PruneNonCodeLocations();

    // A software breakpoint is a plain memory write, which silently has no
    // effect on flash (see KNOWN_ISSUE_AUTONOMOUS_HALT_DETECTION.md), so an
    // explicit user choice always wins; absent one, a configured device
    // memory map picks software for RAM-only locations to conserve FPB
    // comparators, and hardware otherwise - including when no map is
    // configured, which keeps prior behavior unchanged.
    const std::optional<bool> &user_choice = m_context.Session().configuration.requireHardwareBreakpoints;
    const bool want_hardware =
        user_choice ? *user_choice : RequiresHardware(m_bp, m_context.Device().Memory());

    if (lldb::SBError hw_error = m_bp.SetIsHardware(want_hardware); want_hardware && hw_error.Fail()) {
      const char *msg = hw_error.GetCString();
      m_hardware_error =
          msg ? std::string(msg) : "failed to allocate a hardware breakpoint";
      m_context.LogDiagnostic(
          "breakpoint " + std::to_string(m_bp.GetID()) + ": " + m_hardware_error);
    }

    m_bp.AddName(kDAPBreakpointLabel);
    if (!m_condition.empty())
      SetCondition();
    if (!m_hit_condition.empty())
      SetHitCondition();
  });
}

}  // namespace core
