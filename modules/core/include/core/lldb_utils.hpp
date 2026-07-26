//===-- lldb_utils.hpp ---------------------------------------------------===//
//
// Small standalone lldb::SB* helpers shared by multiple core components -
// relocated piecemeal from debug_service/lldb_utils.hpp as components need
// them (that header's remaining contents are mostly not yet needed outside
// debug_service, so they stay there for now).
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_LLDB_UTILS_HPP_
#define TRAILER_CORE_LLDB_UTILS_HPP_

#include <optional>
#include <string>

#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBTarget.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

namespace core {

/// Gets an SBFileSpec and returns its path as a string.
std::string GetSBFileSpecPath(const lldb::SBFileSpec &file_spec);

/// Gets the line entry for a given address.
lldb::SBLineEntry GetLineEntryForAddress(lldb::SBTarget &target, const lldb::SBAddress &address);

/// Get the stop-disassembly-display setting.
lldb::StopDisassemblyType GetStopDisassemblyDisplay(lldb::SBDebugger &debugger);

/// Whether assembly source (rather than the line_entry's file) should be
/// displayed for the given line entry, per the debugger's
/// stop-disassembly-display setting.
bool DisplayAssemblySource(lldb::SBDebugger &debugger, lldb::SBLineEntry line_entry);

/// Formats a load address as a "0x..."-prefixed, zero-padded string.
std::string GetLoadAddressString(lldb::addr_t addr);

/// Builds a protocol::Source from a file spec, or nullopt if the file spec
/// is invalid.
std::optional<dap::protocol::Source> CreateSource(const lldb::SBFileSpec &file);

/// Per the DAP spec, a source must have either `path` or `sourceReference`
/// specified: `path` for sources with known source code, `sourceReference`
/// when falling back to assembly. Returns whether this source is the latter.
bool IsAssemblySource(const dap::protocol::Source &source);

/// A DAP frame id encodes both a thread index ID and a frame index, packed
/// into a single integer.
uint64_t MakeDAPFrameID(lldb::SBFrame &frame);
uint32_t GetLLDBThreadIndexID(uint64_t dap_frame_id);
uint32_t GetLLDBFrameID(uint64_t dap_frame_id);

/// Runs a list of LLDB commands in the command interpreter, writing the
/// prefix, prompt + command and command output for each into `strm`. See
/// the `bool &required_command_failed` overload below for the command
/// prefix (`!`/`?`) semantics.
bool RunLLDBCommands(lldb::SBDebugger &debugger, llvm::StringRef prefix, const llvm::ArrayRef<std::string> &commands,
                     llvm::raw_ostream &strm, bool parse_command_directives, bool echo_commands);

/// Same as above, returning the accumulated output as a string instead of
/// writing to a stream. Each command can be prefixed with `!` (always show
/// output, stop on failure) and/or `?` (only show output on failure).
std::string RunLLDBCommands(lldb::SBDebugger &debugger, llvm::StringRef prefix, const llvm::ArrayRef<std::string> &commands,
                            bool &required_command_failed, bool parse_command_directives = true,
                            bool echo_commands = false);

/// RAII utility to put the debugger temporarily into synchronous mode.
class ScopeSyncMode {
public:
  explicit ScopeSyncMode(lldb::SBDebugger &debugger);
  ~ScopeSyncMode();

  ScopeSyncMode(const ScopeSyncMode &) = delete;
  ScopeSyncMode &operator=(const ScopeSyncMode &) = delete;

private:
  lldb::SBDebugger &m_debugger;
  bool m_async;
};

/// Take ownership of the stored error.
llvm::Error ToError(const lldb::SBError &error, bool show_user = true);

/// Serializes the target's `$__lldb_statistics` dump to a JSON string, or an
/// empty string if the target has no statistics available. Returned as a
/// string (not a llvm::json::Value) so it can cross into
/// core::TerminatedEvent (core/event_bus.hpp) without core::EventBus itself
/// needing to know about llvm::json - see TargetManager::SendTerminatedEvent
/// and the event_translator's TerminatedEvent arm, which re-parses it once
/// when building the wire event.
std::string BuildTerminatedStatisticsJSON(lldb::SBTarget &target);

}  // namespace core

#endif  // TRAILER_CORE_LLDB_UTILS_HPP_
