#include "core/debug_context.hpp"

#include "core/components/breakpoint_manager.hpp"
#include "core/components/data_manager.hpp"
#include "core/components/disassembly_manager.hpp"
#include "core/components/execution_controller.hpp"
#include "core/components/memory_manager.hpp"
#include "core/components/module_manager.hpp"
#include "core/components/target_manager.hpp"
#include "core/lldb_utils.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "lldb/API/SBProcess.h"
#include "lldb/lldb-defines.h"
#include "llvm/Support/FormatVariadic.h"

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

void DebugContext::Send(const dap::protocol::Message &message) {
  event_bus_.Publish(WireMessageEvent{message});
}

void DebugContext::SendJSON(const llvm::json::Value &json) {
  dap::protocol::Message message;
  llvm::json::Path::Root root;
  if (!fromJSON(json, message, root)) {
    LogDiagnostic(llvm::formatv("core: encoding failed: {0}", llvm::toString(root.getError())).str());
    return;
  }
  Send(message);
}

lldb::SBThread DebugContext::GetLLDBThread(lldb::tid_t tid) {
  return lldb_provider_.target.GetProcess().GetThreadByID(tid);
}

lldb::SBThread DebugContext::GetLLDBThread(const llvm::json::Object &arguments) {
  const lldb::tid_t tid = arguments.getInteger("threadId").value_or(LLDB_INVALID_THREAD_ID);
  return lldb_provider_.target.GetProcess().GetThreadByID(tid);
}

lldb::SBFrame DebugContext::GetLLDBFrame(uint64_t dap_frame_id) {
  if (dap_frame_id == LLDB_DAP_INVALID_FRAME_ID)
    return lldb::SBFrame();
  lldb::SBProcess process = lldb_provider_.target.GetProcess();
  lldb::SBThread thread = process.GetThreadByIndexID(GetLLDBThreadIndexID(dap_frame_id));
  return thread.GetFrameAtIndex(GetLLDBFrameID(dap_frame_id));
}

lldb::SBFrame DebugContext::GetLLDBFrame(const llvm::json::Object &arguments) {
  const uint64_t dap_frame_id = arguments.getInteger("frameId").value_or(LLDB_DAP_INVALID_FRAME_ID);
  return GetLLDBFrame(dap_frame_id);
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

}  // namespace core
