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

std::string GetSBFileSpecPath(const lldb::SBFileSpec &file_spec);

lldb::SBLineEntry GetLineEntryForAddress(lldb::SBTarget &target, const lldb::SBAddress &address);

lldb::StopDisassemblyType GetStopDisassemblyDisplay(lldb::SBDebugger &debugger);

bool DisplayAssemblySource(lldb::SBDebugger &debugger, lldb::SBLineEntry line_entry);

std::string GetLoadAddressString(lldb::addr_t addr);

std::optional<dap::protocol::Source> CreateSource(const lldb::SBFileSpec &file);

bool IsAssemblySource(const dap::protocol::Source &source);

uint64_t MakeDAPFrameID(lldb::SBFrame &frame);
uint32_t GetLLDBThreadIndexID(uint64_t dap_frame_id);
uint32_t GetLLDBFrameID(uint64_t dap_frame_id);

bool RunLLDBCommands(lldb::SBDebugger &debugger, llvm::StringRef prefix, const llvm::ArrayRef<std::string> &commands,
                     llvm::raw_ostream &strm, bool parse_command_directives, bool echo_commands);

std::string RunLLDBCommands(lldb::SBDebugger &debugger, llvm::StringRef prefix, const llvm::ArrayRef<std::string> &commands,
                            bool &required_command_failed, bool parse_command_directives = true,
                            bool echo_commands = false);

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

llvm::Error ToError(const lldb::SBError &error, bool show_user = true);

std::string BuildTerminatedStatisticsJSON(lldb::SBTarget &target);

}  // namespace core

#endif  // TRAILER_CORE_LLDB_UTILS_HPP_
