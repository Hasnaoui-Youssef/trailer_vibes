#ifndef TRAILER_CORE_DEBUG_CONTEXT_HPP_
#define TRAILER_CORE_DEBUG_CONTEXT_HPP_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "core/event_bus.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"
#include "lldb/lldb-types.h"
#include "lldb_provider/lldb_provider.hpp"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "openocd_provider/openocd_provider.hpp"

namespace core {

namespace protocol = dap::protocol;

class BreakpointManager;
class MemoryManager;
class DisassemblyManager;
class ModuleManager;
class DataManager;
class ExecutionController;
class TargetManager;
class TraceManager;
class WatchManager;

class DebugContext {
public:
  DebugContext();
  ~DebugContext();

  DebugContext(const DebugContext &) = delete;
  DebugContext &operator=(const DebugContext &) = delete;

  providers::LldbProvider &Lldb() { return lldb_provider_; }
  lldb::SBTarget &Target() { return lldb_provider_.target; }

  // Forwards to LldbProvider::WithTarget() - see there for the contract.
  // DebugContext doesn't own the lock, it just composes the provider that
  // does; managers that only hold a DebugContext& (not a LldbProvider&)
  // still need a way to reach it.
  template <typename Fn>
  auto WithTarget(Fn &&fn) -> std::invoke_result_t<Fn> {
    return lldb_provider_.WithTarget(std::forward<Fn>(fn));
  }

  llvm::Error CreateOpenOcd(const providers::OpenOcdConfig &config);
  providers::OpenOcdProvider *OpenOcd() { return openocd_provider_ ? &*openocd_provider_ : nullptr; }
  void ShutdownOpenOcd();

  llvm::Error CreateTrace();
  TraceManager *Trace() { return trace_manager_.get(); }
  void ShutdownTrace();
  dap::protocol::TraceStatusResponseBody TraceStatus();

  EventBus &Events() { return event_bus_; }
  void SendOutput(OutputCategory category, llvm::StringRef text);
  void Emit(DomainEvent event);

  BreakpointManager &Breakpoints() { return *breakpoint_manager_; }
  MemoryManager &Memory() { return *memory_manager_; }
  DisassemblyManager &Disassembly() { return *disassembly_manager_; }
  ModuleManager &Modules() { return *module_manager_; }
  DataManager &Data() { return *data_manager_; }
  ExecutionController &Execution() { return *execution_controller_; }
  // Named Session(), not Target(): Target() already returns lldb::SBTarget&.
  TargetManager &Session() { return *target_manager_; }
  WatchManager &Watch() { return *watch_manager_; }

  lldb::SBThread GetLLDBThread(lldb::tid_t tid);
  lldb::SBFrame GetLLDBFrame(uint64_t dap_frame_id);

  std::optional<dap::protocol::Source> ResolveSource(const lldb::SBFrame &frame);
  std::optional<dap::protocol::Source> ResolveSource(lldb::SBAddress address);
  std::optional<lldb::addr_t> GetSourceReferenceAddress(int32_t reference);

  bool AutoVariableSummaries() const { return auto_variable_summaries_; }
  void SetAutoVariableSummaries(bool value) { auto_variable_summaries_ = value; }

  llvm::StringRef CommandEscapePrefix() const { return command_escape_prefix_; }
  void SetCommandEscapePrefix(llvm::StringRef value) { command_escape_prefix_ = value.str(); }

  using RunCommandsFn = std::function<void()>;
  void RunStopCommands();
  void RunExitCommands();

  using LogDiagnosticFn = std::function<void(std::string)>;
  void SetLogDiagnostic(LogDiagnosticFn fn) { log_diagnostic_ = std::move(fn); }
  void LogDiagnostic(std::string message) { log_diagnostic_(std::move(message)); }

  using StringFn = std::function<llvm::StringRef()>;
  void SetClientName(StringFn fn) { client_name_ = std::move(fn); }
  llvm::StringRef ClientName() const { return client_name_(); }

  bool HasLastLaunchRequest() const;

  using ClientFeatureEnabledFn = std::function<bool(dap::protocol::ClientFeature)>;
  void SetClientFeatureEnabled(ClientFeatureEnabledFn fn) { client_feature_enabled_ = std::move(fn); }
  bool ClientFeatureEnabled(dap::protocol::ClientFeature feature) const {
    return client_feature_enabled_(feature);
  }

  using CustomCapabilitiesFn = std::function<dap::protocol::Capabilities()>;
  void SetGetCustomCapabilities(CustomCapabilitiesFn fn) { get_custom_capabilities_ = std::move(fn); }
  dap::protocol::Capabilities GetCustomCapabilities() const { return get_custom_capabilities_(); }

  void SendTerminatedEvent();

  void SetRequestStop(RunCommandsFn fn) { request_stop_ = std::move(fn); }
  void RequestStop() { request_stop_(); }

  void SetFrameFormat(llvm::StringRef format);
  void SetThreadFormat(llvm::StringRef format);

  bool IsInterruptRequested();
  void CancelInterruptRequest();
  std::string ExecutablePath();
  std::string TargetTriple();
  std::optional<uint64_t> ProcessId();
  std::vector<dap::protocol::ExceptionBreakpointsFilter> ExceptionBreakpointFilters();
  std::string LldbVersionString();

private:
  providers::LldbProvider lldb_provider_;
  // Declared here, not with the components below: members destroy in
  // reverse declaration order, and ExecutionController's event thread must
  // be joined (a component destructor) before this provider can safely go
  // away, since that thread may still be inside an OpenOCD provider call.
  std::optional<providers::OpenOcdProvider> openocd_provider_;
  EventBus event_bus_;
  bool auto_variable_summaries_ = false;
  std::string command_escape_prefix_;
  LogDiagnosticFn log_diagnostic_;
  StringFn client_name_;
  ClientFeatureEnabledFn client_feature_enabled_;
  CustomCapabilitiesFn get_custom_capabilities_;
  RunCommandsFn request_stop_;

  std::unique_ptr<BreakpointManager> breakpoint_manager_;
  std::unique_ptr<MemoryManager> memory_manager_;
  std::unique_ptr<DisassemblyManager> disassembly_manager_;
  std::unique_ptr<ModuleManager> module_manager_;
  std::unique_ptr<DataManager> data_manager_;
  std::unique_ptr<ExecutionController> execution_controller_;
  std::unique_ptr<TargetManager> target_manager_;
  // watch_manager_ and trace_manager_ are declared last (destroy first):
  // both own threads that call into openocd_provider_, which must not go
  // away while either is still running.
  std::unique_ptr<WatchManager> watch_manager_;
  // Its destructor synchronously unsubscribes from the OpenOCD capture
  // callback, which must happen before disassembly_manager_ (and
  // everything else above) is torn down.
  std::unique_ptr<TraceManager> trace_manager_;
  std::string trace_unavailable_reason_;
};

}  // namespace core

#endif  // TRAILER_CORE_DEBUG_CONTEXT_HPP_
