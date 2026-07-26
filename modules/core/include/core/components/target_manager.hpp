//===-- target_manager.hpp -------------------------------------------------===//
//
// The target component (see CLAUDE.md's DebugContext architecture): target
// lifecycle (create/attach/launch/disconnect/restart), launch/attach
// configuration and the Run*Commands family. Carved out of DebugService -
// see PROJECT_STATUS.md.
//
// Named TargetManager per the plan, but exposed from DebugContext as
// Session() rather than Target(): DebugContext::Target() already returns the
// raw lldb::SBTarget& (see its own comment - every component reaches the
// provider directly, "no abstraction beneath a provider"), and every other
// component accessor drops the Manager/Controller suffix
// (BreakpointManager -> Breakpoints(), ExecutionController -> Execution()),
// which would collide here.
//
//===-------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_TARGET_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_TARGET_MANAGER_HPP_

#include <optional>
#include <string>

#include "dap/protocol/protocol_requests.hpp"
#include "lldb/API/SBError.h"
#include "lldb/API/SBTarget.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Threading.h"

namespace core {

class DebugContext;

class TargetManager {
public:
  explicit TargetManager(DebugContext &context);

  TargetManager(const TargetManager &) = delete;
  TargetManager &operator=(const TargetManager &) = delete;

  /// Configuration specified by the launch or attach commands.
  dap::protocol::Configuration configuration;

  /// A copy of the last LaunchRequest so we can reuse its arguments if we
  /// get a RestartRequest. Restarting an AttachRequest is not supported.
  std::optional<dap::protocol::LaunchRequestArguments> last_launch_request;

  bool is_attach = false;

  /// Set while restarting, to distinguish the "exited" event for the
  /// process being killed from a real session-ending exit - see
  /// ExecutionController::HandleProcessEvent (reached via
  /// DebugContext::RestartingProcessId/SetRestartingProcessId, which forward
  /// directly here now that this component owns it for real).
  lldb::pid_t restarting_process_id = LLDB_INVALID_PROCESS_ID;

  bool no_lldbinit = false;
  bool source_init_file = true;

  /// Configures the debug adapter for launching/attaching.
  void SetConfiguration(const dap::protocol::Configuration &config, bool is_attach);
  void ConfigureSourceMaps();

  lldb::SBTarget CreateTarget(lldb::SBError &error);
  void SetTarget(lldb::SBTarget target);

  llvm::Error Disconnect();
  llvm::Error Disconnect(bool terminate_debuggee);

  // Takes a LaunchRequest and launches the process (including running
  // launchCommands if given), handling everything short of the additional
  // request-level bookkeeping request_launch does around this call. Reused
  // by RestartRequest too. Locks the API mutex internally for its full
  // duration - see debug_context.hpp's class comment on the universal
  // internal-locking rule.
  llvm::Error LaunchProcess(const dap::protocol::LaunchRequestArguments &arguments);

  // Attaches to an already-running debuggee (by pid, gdb-remote connection,
  // core file, or attachCommands), handling everything short of the
  // request-level PrintWelcomeMessage() the handler prints around this
  // call. Locks the API mutex internally for its full duration.
  llvm::Error Attach(const dap::protocol::AttachRequestArguments &arguments);

  // Creates the target and launches it (see LaunchProcess above), handling
  // everything short of the request-level PrintWelcomeMessage() the handler
  // prints around this call. Locks the API mutex internally for its full
  // duration (lldb::SBMutex is recursive, so calling the also-locking
  // LaunchProcess from within is safe).
  llvm::Error Launch(const dap::protocol::LaunchRequestArguments &arguments);

  // Restarts a debug session: kills the current process (if any) and
  // re-launches it with the last launch request's (possibly updated)
  // arguments. Restarting an attach isn't supported (matches upstream).
  // Locks the API mutex internally for its full duration.
  llvm::Error Restart(const std::optional<dap::protocol::RestartArguments> &arguments);

  // Runs RunTerminateCommands and sends the "terminated" event exactly once
  // per session. Also called from Disconnect() above.
  void SendTerminatedEvent();

  bool RunLLDBCommands(llvm::StringRef prefix, llvm::ArrayRef<std::string> commands);
  llvm::Error RunAttachCommands(llvm::ArrayRef<std::string> attach_commands);
  llvm::Error RunLaunchCommands(llvm::ArrayRef<std::string> launch_commands);
  llvm::Error RunPreInitCommands();
  llvm::Error RunInitCommands();
  llvm::Error RunPreRunCommands();
  void RunPostRunCommands();
  void RunStopCommands();
  void RunExitCommands();
  void RunTerminateCommands();

  llvm::Error InitializeDebugger();

private:
  DebugContext &m_context;
  llvm::once_flag m_terminated_event_flag;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_TARGET_MANAGER_HPP_
