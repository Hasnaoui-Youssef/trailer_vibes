#ifndef TRAILER_CORE_COMPONENTS_TARGET_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_TARGET_MANAGER_HPP_

#include <chrono>
#include <optional>
#include <string>

#include "dap/protocol/protocol_requests.hpp"
#include "lldb/API/SBError.h"
#include "lldb/API/SBTarget.h"
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

  dap::protocol::Configuration configuration;

  std::optional<dap::protocol::LaunchRequestArguments> last_launch_request;

  bool is_attach = false;

  bool no_lldbinit = false;
  bool source_init_file = true;

  void SetConfiguration(const dap::protocol::Configuration &config, bool is_attach);
  void ConfigureSourceMaps();
  llvm::Error ConfigureHardwareBreakpointRequirement();

  lldb::SBTarget CreateTarget(lldb::SBError &error);
  void SetTarget(lldb::SBTarget target);

  llvm::Error Disconnect();
  llvm::Error Disconnect(bool terminate_debuggee);

  llvm::Error ConnectToGdbRemote(llvm::StringRef hostname, llvm::StringRef port, std::chrono::seconds timeout);

  llvm::Error Attach(const dap::protocol::AttachRequestArguments &arguments);

  llvm::Error Launch(const dap::protocol::LaunchRequestArguments &arguments);

  llvm::Error Restart(const std::optional<dap::protocol::RestartArguments> &arguments);

  void SendTerminatedEvent();

  bool RunLLDBCommands(llvm::StringRef prefix, llvm::ArrayRef<std::string> commands);
  llvm::Error RunAttachCommands(llvm::ArrayRef<std::string> attach_commands);
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
