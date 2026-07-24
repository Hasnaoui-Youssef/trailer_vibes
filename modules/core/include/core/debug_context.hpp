//===-- debug_context.hpp -----------------------------------------------===//
//
// The engine's runtime root (see CLAUDE.md's DebugContext architecture).
// Owns the LldbProvider, the event bus, and every domain component as it
// gets carved out of debug_service::DebugService (see PROJECT_STATUS.md for
// which components exist yet). Handlers dispatch to DebugContext instead of
// DebugService for whatever has already been carved; components reach each
// other only through DebugContext (the mediator), never directly.
//
// DebugContext never depends on debug_service or dap_core: it must stay
// usable without any particular communication layer on top of it. For
// domain queries that a not-yet-carved component still owns, DebugContext
// exposes a narrow callback seam (see AutoVariableSummaries below for the
// one remaining example) instead of holding a reference to the concrete
// owner - dap_handlers' DebugServiceSessionImpl (which is allowed to
// depend on both debug_service and core) wires the current implementation
// in at construction time, and re-wires it - or, once the owning component
// is carved (as ResolveSource/GetSourceReferenceAddress now are, forwarded
// directly to ModuleManager below), removes the seam entirely. This is
// what "DebugContext initially wraps DebugService" (see the plan's phase
// 3) means in practice once core is not permitted to hold a DebugService&
// at all.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_DEBUG_CONTEXT_HPP_
#define TRAILER_CORE_DEBUG_CONTEXT_HPP_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "core/event_bus.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBMutex.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"
#include "lldb/lldb-types.h"
#include "lldb_provider/lldb_provider.hpp"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

namespace core {

// Precise alias (not a `using namespace` directive - no ambiguity risk),
// so core code can write the same unqualified `protocol::Foo` the original
// forked lldb-dap code did. See modules/dap/CMakeLists.txt for why core is
// allowed to depend on dap_protocol's plain value-type vocabulary at all.
namespace protocol = dap::protocol;

class BreakpointManager;
class MemoryManager;
class DisassemblyManager;
class ModuleManager;
class DataManager;
class ExecutionController;
class TargetManager;

class DebugContext {
public:
  DebugContext();
  ~DebugContext();

  DebugContext(const DebugContext &) = delete;
  DebugContext &operator=(const DebugContext &) = delete;

  // --- Provider access (every component holds this directly too - see
  // CLAUDE.md: "no abstraction beneath a provider" - DebugContext exposing
  // it as well is a convenience for code that doesn't hold its own
  // LldbProvider&, not a required indirection). ---
  providers::LldbProvider &Lldb() { return lldb_provider_; }
  lldb::SBTarget &Target() { return lldb_provider_.target; }
  lldb::SBMutex GetAPIMutex() const { return lldb_provider_.GetAPIMutex(); }

  // --- Event bus (mediator's outbound side - see event_bus.hpp). ---
  EventBus &Events() { return event_bus_; }
  void SendOutput(OutputCategory category, llvm::StringRef text);

  // Publishes a WireMessageEvent - see event_bus.hpp for why this carries
  // an already-typed protocol::Message rather than a bespoke struct per
  // event kind. Mirrors DebugService::Send/SendJSON's signatures exactly
  // so relocated Send*Event functions need no call-site changes beyond
  // `dap.Send(...)` -> `m_context.Send(...)`.
  void Send(const dap::protocol::Message &message);
  void SendJSON(const llvm::json::Value &json);

  // --- Components (populated one carve at a time - see
  // PROJECT_STATUS.md). ---
  BreakpointManager &Breakpoints() { return *breakpoint_manager_; }
  MemoryManager &Memory() { return *memory_manager_; }
  DisassemblyManager &Disassembly() { return *disassembly_manager_; }
  ModuleManager &Modules() { return *module_manager_; }
  DataManager &Data() { return *data_manager_; }
  ExecutionController &Execution() { return *execution_controller_; }
  // Named Session(), not Target() - see target_manager.hpp's class comment
  // for why (Target() already returns the raw lldb::SBTarget&).
  TargetManager &Session() { return *target_manager_; }

  // Thread/frame lookup by DAP id - pure LldbProvider utilities with no
  // Data-state involvement, so exposed here directly rather than nested
  // under DataManager; used broadly (execution, breakpoints, data
  // handlers alike).
  lldb::SBThread GetLLDBThread(lldb::tid_t tid);
  lldb::SBThread GetLLDBThread(const llvm::json::Object &arguments);
  lldb::SBFrame GetLLDBFrame(uint64_t dap_frame_id);
  lldb::SBFrame GetLLDBFrame(const llvm::json::Object &arguments);

  // Forwarded directly to ModuleManager now that it's carved (see class
  // comment) - callers that were wired against the old callback seam don't
  // need to change, only DebugServiceSessionImpl's construction-time wiring
  // did.
  std::optional<dap::protocol::Source> ResolveSource(const lldb::SBFrame &frame);
  std::optional<dap::protocol::Source> ResolveSource(lldb::SBAddress address);
  std::optional<lldb::addr_t> GetSourceReferenceAddress(int32_t reference);

  // --- Narrow settings mirror for cross-cutting flags read by
  // already-carved components before TargetManager (which will own
  // Configuration in full) is carved. ---
  bool AutoVariableSummaries() const { return auto_variable_summaries_; }
  void SetAutoVariableSummaries(bool value) { auto_variable_summaries_ = value; }

  llvm::StringRef CommandEscapePrefix() const { return command_escape_prefix_; }
  void SetCommandEscapePrefix(llvm::StringRef value) { command_escape_prefix_ = value.str(); }

  // Forwarded directly to TargetManager now that it's carved (see class
  // comment on ResolveSource above for the general pattern).
  lldb::pid_t RestartingProcessId() const;
  void SetRestartingProcessId(lldb::pid_t value);

  using RunCommandsFn = std::function<void()>;

  // Forwarded directly to TargetManager - kept as plain methods (not a
  // seam) since ExecutionController's event pump calls into them and
  // TargetManager now owns the real implementation.
  void RunStopCommands();
  void RunExitCommands();

  // Diagnostic (stderr-only, not part of the DAP wire protocol) logging
  // seam - wired to DebugService::log today; not worth carrying a full
  // dap::Log-alike into core for two call sites.
  using LogDiagnosticFn = std::function<void(std::string)>;
  void SetLogDiagnostic(LogDiagnosticFn fn) { log_diagnostic_ = std::move(fn); }
  void LogDiagnostic(std::string message) { log_diagnostic_(std::move(message)); }

  // --- Further forwarding seams for not-yet-carved TargetManager/
  // communication-layer state ExecutionController's event pump reads.
  // Same removed-once-carved lifecycle as the seams above. ---
  using StringFn = std::function<llvm::StringRef()>;
  void SetClientName(StringFn fn) { client_name_ = std::move(fn); }
  llvm::StringRef ClientName() const { return client_name_(); }

  // Forwarded directly to TargetManager now that it's carved.
  bool HasLastLaunchRequest() const;

  using ClientFeatureEnabledFn = std::function<bool(dap::protocol::ClientFeature)>;
  void SetClientFeatureEnabled(ClientFeatureEnabledFn fn) { client_feature_enabled_ = std::move(fn); }
  bool ClientFeatureEnabled(dap::protocol::ClientFeature feature) const {
    return client_feature_enabled_(feature);
  }

  using CustomCapabilitiesFn = std::function<dap::protocol::Capabilities()>;
  void SetGetCustomCapabilities(CustomCapabilitiesFn fn) { get_custom_capabilities_ = std::move(fn); }
  dap::protocol::Capabilities GetCustomCapabilities() const { return get_custom_capabilities_(); }

  // Runs RunTerminateCommands + sends the "terminated" event exactly once
  // per session (TargetManager-domain, but entangled with communication-only
  // state - terminated_event_flag, CreateTerminatedEventObject - that stays
  // on DebugService for now; also called from TargetManager::Disconnect()).
  void SetSendTerminatedEvent(RunCommandsFn fn) { send_terminated_event_ = std::move(fn); }
  void SendTerminatedEvent() { send_terminated_event_(); }

  // TargetManager::Disconnect() needs to stop the Orchestrator's read loop
  // once the debuggee is killed/detached - a communication-layer action
  // core has no other way to reach.
  void SetRequestStop(RunCommandsFn fn) { request_stop_ = std::move(fn); }
  void RequestStop() { request_stop_(); }

  // frame_format/thread_format parsing (with user-facing error reporting on
  // a bad format string) stay on DebugService - they belong to DataManager
  // per the plan, not carved this pass - but TargetManager::SetConfiguration
  // needs to apply a custom format string from the launch/attach config.
  using StringConsumerFn = std::function<void(llvm::StringRef)>;
  void SetSetFrameFormat(StringConsumerFn fn) { set_frame_format_ = std::move(fn); }
  void SetFrameFormat(llvm::StringRef format) { set_frame_format_(format); }
  void SetSetThreadFormat(StringConsumerFn fn) { set_thread_format_ = std::move(fn); }
  void SetThreadFormat(llvm::StringRef format) { set_thread_format_(format); }

private:
  providers::LldbProvider lldb_provider_;
  EventBus event_bus_;
  bool auto_variable_summaries_ = false;
  std::string command_escape_prefix_;
  LogDiagnosticFn log_diagnostic_;
  StringFn client_name_;
  ClientFeatureEnabledFn client_feature_enabled_;
  CustomCapabilitiesFn get_custom_capabilities_;
  RunCommandsFn send_terminated_event_;
  RunCommandsFn request_stop_;
  StringConsumerFn set_frame_format_;
  StringConsumerFn set_thread_format_;

  std::unique_ptr<BreakpointManager> breakpoint_manager_;
  std::unique_ptr<MemoryManager> memory_manager_;
  std::unique_ptr<DisassemblyManager> disassembly_manager_;
  std::unique_ptr<ModuleManager> module_manager_;
  std::unique_ptr<DataManager> data_manager_;
  std::unique_ptr<ExecutionController> execution_controller_;
  std::unique_ptr<TargetManager> target_manager_;
};

}  // namespace core

#endif  // TRAILER_CORE_DEBUG_CONTEXT_HPP_
