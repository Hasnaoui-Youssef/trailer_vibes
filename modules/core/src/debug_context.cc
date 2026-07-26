#include "core/debug_context.hpp"

#include "core/components/breakpoint_manager.hpp"
#include "core/components/data_manager.hpp"
#include "core/components/disassembly_manager.hpp"
#include "core/components/execution_controller.hpp"
#include "core/components/exception_breakpoint.hpp"
#include "core/components/memory_manager.hpp"
#include "core/components/module_manager.hpp"
#include "core/components/target_manager.hpp"
#include "core/lldb_utils.hpp"
#include "dap/dap_error.hpp"
#include "dap/protocol/protocol_events.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBMutex.h"
#include "lldb/API/SBProcess.h"
#include "llvm/Support/Base64.h"

namespace core {

DebugContext::DebugContext()
    : breakpoint_manager_(std::make_unique<BreakpointManager>(*this)),
      memory_manager_(std::make_unique<MemoryManager>(lldb_provider_)),
      disassembly_manager_(std::make_unique<DisassemblyManager>(*this)),
      module_manager_(std::make_unique<ModuleManager>(lldb_provider_)),
      data_manager_(std::make_unique<DataManager>(*this)),
      execution_controller_(std::make_unique<ExecutionController>(*this)),
      target_manager_(std::make_unique<TargetManager>(*this)) {}

// Defined here (not defaulted in the header) because BreakpointManager,
// MemoryManager, DisassemblyManager, ModuleManager, DataManager,
// ExecutionController and TargetManager are only forward-declared there -
// std::unique_ptr's deleter needs the complete type, which this TU has via
// the includes above.
DebugContext::~DebugContext() = default;

lldb::pid_t DebugContext::RestartingProcessId() const { return target_manager_->restarting_process_id; }
void DebugContext::SetRestartingProcessId(lldb::pid_t value) { target_manager_->restarting_process_id = value; }

void DebugContext::RunStopCommands() { target_manager_->RunStopCommands(); }
void DebugContext::RunExitCommands() { target_manager_->RunExitCommands(); }

bool DebugContext::HasLastLaunchRequest() const { return target_manager_->last_launch_request.has_value(); }

void DebugContext::Emit(DomainEvent event) { event_bus_.Publish(event); }

lldb::SBThread DebugContext::GetLLDBThread(lldb::tid_t tid) {
  return lldb_provider_.target.GetProcess().GetThreadByID(tid);
}

lldb::SBFrame DebugContext::GetLLDBFrame(uint64_t dap_frame_id) {
  if (dap_frame_id == LLDB_DAP_INVALID_FRAME_ID)
    return lldb::SBFrame();
  lldb::SBProcess process = lldb_provider_.target.GetProcess();
  lldb::SBThread thread = process.GetThreadByIndexID(GetLLDBThreadIndexID(dap_frame_id));
  return thread.GetFrameAtIndex(GetLLDBFrameID(dap_frame_id));
}

void DebugContext::SendOutput(OutputCategory category, llvm::StringRef text) {
  if (text.empty())
    return;
  event_bus_.Publish(OutputEvent{category, text.str()});
}

std::optional<dap::protocol::Source> DebugContext::ResolveSource(const lldb::SBFrame &frame) {
  return module_manager_->ResolveSource(frame);
}

std::optional<dap::protocol::Source> DebugContext::ResolveSource(lldb::SBAddress address) {
  return module_manager_->ResolveSource(address);
}

std::optional<lldb::addr_t> DebugContext::GetSourceReferenceAddress(int32_t reference) {
  return module_manager_->GetSourceReferenceAddress(reference);
}

void DebugContext::SendTerminatedEvent() { target_manager_->SendTerminatedEvent(); }

void DebugContext::SetFrameFormat(llvm::StringRef format) { data_manager_->SetFrameFormat(format); }
void DebugContext::SetThreadFormat(llvm::StringRef format) { data_manager_->SetThreadFormat(format); }

bool DebugContext::IsInterruptRequested() { return lldb_provider_.debugger.InterruptRequested(); }
void DebugContext::CancelInterruptRequest() { lldb_provider_.debugger.CancelInterruptRequest(); }

std::string DebugContext::ExecutablePath() {
  lldb::SBMutex lock = GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);
  return GetSBFileSpecPath(lldb_provider_.target.GetExecutable());
}

std::string DebugContext::TargetTriple() {
  lldb::SBMutex lock = GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);
  const char *triple = lldb_provider_.target.GetTriple();
  return triple ? triple : "";
}

std::optional<uint64_t> DebugContext::ProcessId() {
  lldb::SBMutex lock = GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);
  lldb::SBProcess process = lldb_provider_.target.GetProcess();
  if (!process.IsValid())
    return std::nullopt;
  return process.GetProcessID();
}

std::vector<dap::protocol::ExceptionBreakpointsFilter> DebugContext::ExceptionBreakpointFilters() {
  lldb::SBMutex lock = GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  breakpoint_manager_->PopulateExceptionBreakpoints();
  std::vector<dap::protocol::ExceptionBreakpointsFilter> filters;
  for (const auto &exc_bp : breakpoint_manager_->exception_breakpoints)
    filters.emplace_back(CreateExceptionBreakpointFilter(exc_bp));
  return filters;
}

std::string DebugContext::LldbVersionString() { return lldb_provider_.debugger.GetVersionString(); }

}  // namespace core
