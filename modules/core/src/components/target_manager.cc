#include "core/components/target_manager.hpp"

#include <mutex>
#include <utility>

#include "core/components/execution_controller.hpp"
#include "core/debug_context.hpp"
#include "core/lldb_utils.hpp"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBFile.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBProcess.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

namespace core {

namespace protocol = dap::protocol;

namespace {

llvm::Error CreateRunLLDBCommandsErrorMessage(llvm::StringRef category) {
  return llvm::createStringError(
      llvm::inconvertibleErrorCode(),
      llvm::formatv("Failed to run {0} commands. See the Debug Console for more details.", category).str());
}

}  // namespace

TargetManager::TargetManager(DebugContext &context) : m_context(context) {}

void TargetManager::SetConfiguration(const dap::protocol::Configuration &config, bool is_attach) {
  configuration = config;
  m_context.Execution().stop_at_entry = config.stopOnEntry;
  this->is_attach = is_attach;

  // Mirrors these flags into the already-carved components that need them
  // (e.g. BreakpointManager's log-message formatting, DataManager's REPL
  // mode detection) - see core::DebugContext's class comment on the
  // settings-mirror seam.
  m_context.SetAutoVariableSummaries(config.enableAutoVariableSummaries);
  m_context.SetCommandEscapePrefix(config.commandEscapePrefix);

  if (configuration.customFrameFormat)
    m_context.SetFrameFormat(*configuration.customFrameFormat);
  if (configuration.customThreadFormat)
    m_context.SetThreadFormat(*configuration.customThreadFormat);
}

void TargetManager::ConfigureSourceMaps() {
  if (configuration.sourceMap.empty() && configuration.sourcePath.empty())
    return;

  std::string source_map_command;
  llvm::raw_string_ostream strm(source_map_command);
  strm << "settings set target.source-map ";
  if (!configuration.sourceMap.empty()) {
    for (const auto &kv : configuration.sourceMap)
      strm << "\"" << kv.first << "\" \"" << kv.second << "\" ";
  } else if (!configuration.sourcePath.empty()) {
    strm << "\".\" \"" << configuration.sourcePath << "\"";
  }
  RunLLDBCommands("Setting source map:", {source_map_command});
}

lldb::SBTarget TargetManager::CreateTarget(lldb::SBError &error) {
  return m_context.Lldb().debugger.CreateTarget(
      /*filename=*/configuration.program.data(),
      /*target_triple=*/configuration.targetTriple.data(),
      /*platform_name=*/configuration.platformName.data(),
      /*add_dependent_modules=*/true, error);
}

void TargetManager::SetTarget(lldb::SBTarget target) {
  m_context.Target() = target;
  if (target.IsValid()) {
    lldb::SBListener listener = m_context.Lldb().debugger.GetListener();
    listener.StartListeningForEvents(m_context.Target().GetBroadcaster(),
                                      lldb::SBTarget::eBroadcastBitBreakpointChanged |
                                          lldb::SBTarget::eBroadcastBitModulesLoaded |
                                          lldb::SBTarget::eBroadcastBitModulesUnloaded |
                                          lldb::SBTarget::eBroadcastBitSymbolsLoaded |
                                          lldb::SBTarget::eBroadcastBitSymbolsChanged |
                                          lldb::SBTarget::eBroadcastBitNewTargetCreated);
  }
}

llvm::Error TargetManager::Disconnect() { return Disconnect(!is_attach); }

llvm::Error TargetManager::Disconnect(bool terminate_debuggee) {
  lldb::SBError error;
  lldb::SBProcess process = m_context.Target().GetProcess();
  switch (process.GetState()) {
  case lldb::eStateInvalid:
  case lldb::eStateUnloaded:
  case lldb::eStateDetached:
  case lldb::eStateExited:
    break;
  case lldb::eStateConnected:
  case lldb::eStateAttaching:
  case lldb::eStateLaunching:
  case lldb::eStateStepping:
  case lldb::eStateCrashed:
  case lldb::eStateSuspended:
  case lldb::eStateStopped:
  case lldb::eStateRunning: {
    ScopeSyncMode scope_sync_mode(m_context.Lldb().debugger);
    error = terminate_debuggee ? process.Kill() : process.Detach();
    break;
  }
  }

  m_context.SendTerminatedEvent();
  // Stop the event thread before the Orchestrator's write loop tears down -
  // otherwise it could still be calling SendMessage() on a Transport that's
  // mid-shutdown.
  m_context.Execution().StopEventHandlers();
  m_context.RequestStop();
  return ToError(error);
}

bool TargetManager::RunLLDBCommands(llvm::StringRef prefix, llvm::ArrayRef<std::string> commands) {
  bool required_command_failed = false;
  std::string output = core::RunLLDBCommands(m_context.Lldb().debugger, prefix, commands, required_command_failed,
                                             /*parse_command_directives=*/true, /*echo_commands=*/true);
  m_context.SendOutput(OutputCategory::Console, output);
  return !required_command_failed;
}

llvm::Error TargetManager::RunAttachCommands(llvm::ArrayRef<std::string> attach_commands) {
  if (!RunLLDBCommands("Running attachCommands:", attach_commands))
    return CreateRunLLDBCommandsErrorMessage("attach");
  return llvm::Error::success();
}

llvm::Error TargetManager::RunLaunchCommands(llvm::ArrayRef<std::string> launch_commands) {
  if (!RunLLDBCommands("Running launchCommands:", launch_commands))
    return CreateRunLLDBCommandsErrorMessage("launch");
  return llvm::Error::success();
}

llvm::Error TargetManager::RunPreInitCommands() {
  if (!RunLLDBCommands("Running preInitCommands:", configuration.preInitCommands))
    return CreateRunLLDBCommandsErrorMessage("preInitCommands");
  return llvm::Error::success();
}

llvm::Error TargetManager::RunInitCommands() {
  if (!RunLLDBCommands("Running initCommands:", configuration.initCommands))
    return CreateRunLLDBCommandsErrorMessage("initCommands");
  return llvm::Error::success();
}

llvm::Error TargetManager::RunPreRunCommands() {
  if (!RunLLDBCommands("Running preRunCommands:", configuration.preRunCommands))
    return CreateRunLLDBCommandsErrorMessage("preRunCommands");
  return llvm::Error::success();
}

void TargetManager::RunPostRunCommands() {
  RunLLDBCommands("Running postRunCommands:", configuration.postRunCommands);
}
void TargetManager::RunStopCommands() { RunLLDBCommands("Running stopCommands:", configuration.stopCommands); }
void TargetManager::RunExitCommands() { RunLLDBCommands("Running exitCommands:", configuration.exitCommands); }
void TargetManager::RunTerminateCommands() {
  RunLLDBCommands("Running terminateCommands:", configuration.terminateCommands);
}

llvm::Error TargetManager::InitializeDebugger() {
  lldb::SBDebugger &debugger = m_context.Lldb().debugger;
  debugger = lldb::SBDebugger::Create(/*source_init_files=*/false);

  // Route LLDB's own command-interpreter output to stderr, never stdout -
  // stdout is the DAP channel (see dap_main.cc). Forwarding LLDB's output
  // through as DAP `output` events (like upstream's OutputRedirector does)
  // isn't ported yet.
  debugger.SetOutputFile(lldb::SBFile(stderr, /*transfer_ownership=*/false));
  debugger.SetErrorFile(lldb::SBFile(stderr, /*transfer_ownership=*/false));

  // Disable LLDB's built-in debuginfod client: CreateTarget() otherwise
  // blocks (observed: minutes, not seconds) on an outbound HTTPS lookup to a
  // debuginfod server when a target's debug info is incomplete, which real
  // embedded firmware images with partial/no debug info hit constantly, and
  // which isn't reachable in every network environment this adapter runs in
  // anyway.
  {
    lldb::SBCommandReturnObject result;
    debugger.GetCommandInterpreter().HandleCommand("settings set symbols.enable-external-lookup false", result);
  }

  m_context.Target() = debugger.GetDummyTarget();

  const bool should_source_init_files = !no_lldbinit && source_init_file;
  if (should_source_init_files) {
    debugger.SkipLLDBInitFiles(false);
    debugger.SkipAppInitFiles(false);
    lldb::SBCommandReturnObject init;
    auto interp = debugger.GetCommandInterpreter();
    interp.SourceInitFileInGlobalDirectory(init);
    interp.SourceInitFileInHomeDirectory(init);
  }

  if (llvm::Error err = RunPreInitCommands())
    return err;

  m_context.Execution().StartEventThread();
  return llvm::Error::success();
}

}  // namespace core
