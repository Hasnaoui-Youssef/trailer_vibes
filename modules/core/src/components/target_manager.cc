#include "core/components/target_manager.hpp"

#include <mutex>
#include <utility>
#include <vector>

#include "core/components/execution_controller.hpp"
#include "core/debug_context.hpp"
#include "core/lldb_utils.hpp"
#include "dap/dap_error.hpp"
#include "lldb/API/SBAttachInfo.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBEnvironment.h"
#include "lldb/API/SBFile.h"
#include "lldb/API/SBLaunchInfo.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBMutex.h"
#include "lldb/API/SBProcess.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
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

std::vector<const char *> MakeArgv(const llvm::ArrayRef<std::string> &strs) {
  std::vector<const char *> argv;
  for (const auto &s : strs)
    argv.push_back(s.c_str());
  argv.push_back(nullptr);
  return argv;
}

uint32_t SetLaunchFlag(uint32_t flags, bool flag, lldb::LaunchFlags mask) {
  if (flag)
    flags |= mask;
  else
    flags &= ~mask;
  return flags;
}

void SetupIORedirection(const std::vector<std::optional<std::string>> &stdio, lldb::SBLaunchInfo &launch_info) {
  size_t n = std::max(stdio.size(), static_cast<size_t>(3));
  for (size_t i = 0; i < n; i++) {
    std::optional<std::string> path;
    if (stdio.size() <= i)
      path = stdio.back();
    else
      path = stdio[i];
    if (!path)
      continue;
    switch (i) {
    case 0:
      launch_info.AddOpenFileAction(i, path->c_str(), true, false);
      break;
    case 1:
    case 2:
      launch_info.AddOpenFileAction(i, path->c_str(), false, true);
      break;
    default:
      launch_info.AddOpenFileAction(i, path->c_str(), true, true);
      break;
    }
  }
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

llvm::Error TargetManager::LaunchProcess(const dap::protocol::LaunchRequestArguments &arguments) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  const std::vector<std::string> &launchCommands = arguments.launchCommands;

  lldb::SBLaunchInfo launch_info = m_context.Target().GetLaunchInfo();

  if (!arguments.cwd.empty())
    launch_info.SetWorkingDirectory(arguments.cwd.data());

  if (!arguments.args.empty())
    launch_info.SetArguments(MakeArgv(arguments.args).data(), true);

  if (!arguments.env.empty()) {
    lldb::SBEnvironment env;
    for (const auto &kv : arguments.env)
      env.Set(kv.first().data(), kv.second.c_str(), true);
    launch_info.SetEnvironment(env, true);
  }

  if (!arguments.stdio.empty() && !arguments.disableSTDIO)
    SetupIORedirection(arguments.stdio, launch_info);

  launch_info.SetDetachOnError(arguments.detachOnError);
  launch_info.SetShellExpandArguments(arguments.shellExpandArguments);

  auto flags = launch_info.GetLaunchFlags();
  flags = SetLaunchFlag(flags, arguments.disableASLR, lldb::eLaunchFlagDisableASLR);
  flags = SetLaunchFlag(flags, arguments.disableSTDIO, lldb::eLaunchFlagDisableSTDIO);
  launch_info.SetLaunchFlags(flags | lldb::eLaunchFlagDebug | lldb::eLaunchFlagStopAtEntry);

  {
    // Perform the launch in synchronous mode so that we don't have to worry
    // about process state changes during the launch.
    ScopeSyncMode scope_sync_mode(m_context.Lldb().debugger);

    if (arguments.console != dap::protocol::eConsoleInternal) {
      return llvm::createStringError(llvm::inconvertibleErrorCode(), "runInTerminal is not supported yet");
    } else if (launchCommands.empty()) {
      lldb::SBError error;
      m_context.Target().Launch(launch_info, error);
      if (error.Fail())
        return ToError(error);
    } else {
      // Set the launch info so that run commands can access the configured
      // launch details.
      m_context.Target().SetLaunchInfo(launch_info);
      if (llvm::Error err = RunLaunchCommands(launchCommands))
        return err;

      // The custom commands might have created a new target so we should use
      // the selected target after these commands are run.
      m_context.Target() = m_context.Lldb().debugger.GetSelectedTarget();
    }
  }

  // Make sure the process is launched and stopped at the entry point before
  // proceeding.
  lldb::SBError error = m_context.Execution().WaitForProcessToStop(arguments.configuration.timeout);
  if (error.Fail())
    return ToError(error);

  return llvm::Error::success();
}

llvm::Error TargetManager::Attach(const dap::protocol::AttachRequestArguments &arguments) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  // Session reuse (attaching to a debugger/target from a prior session) isn't
  // ported yet - it depends on DAPSessionManager, which this project doesn't
  // fork (see project-dap-layer-fork-strategy memory: no multi-session
  // acceptor scenario in a stdio-per-process adapter). arguments.session is
  // ignored for now.
  std::optional<dap::protocol::DAPSession> session;

  if (llvm::Error err = InitializeDebugger())
    return err;

  SetConfiguration(arguments.configuration, /*is_attach=*/true);
  if (!arguments.coreFile.empty())
    m_context.Execution().stop_at_entry = true;

  // This is a hack for loading DWARF in .o files on Mac where the .o files
  // in the debug map of the main executable have relative paths which
  // require the lldb-dap binary to have its working directory set to that
  // relative root for the .o files in order to be able to load debug info.
  if (!configuration.debuggerRoot.empty())
    llvm::sys::fs::set_current_path(configuration.debuggerRoot);

  // Run any initialize LLDB commands the user specified in the launch.json
  if (llvm::Error err = RunInitCommands())
    return err;

  ConfigureSourceMaps();

  lldb::SBError error;
  lldb::SBTarget target;
  if (session) {
    // Use the unique target ID to get the target.
    target = m_context.Lldb().debugger.FindTargetByGloballyUniqueID(session->targetId);
    if (!target.IsValid()) {
      error.SetErrorString(llvm::formatv("invalid targetId {0} in attach config", session->targetId).str().c_str());
    }
  } else {
    target = CreateTarget(error);
  }

  if (error.Fail())
    return ToError(error);

  SetTarget(target);

  // Run any pre run LLDB commands the user specified in the launch.json
  if (llvm::Error err = RunPreRunCommands())
    return err;

  if ((arguments.pid == LLDB_INVALID_PROCESS_ID || arguments.gdbRemotePort == LLDB_DAP_INVALID_PORT) &&
      arguments.waitFor)
    m_context.SendOutput(OutputCategory::Console,
                         llvm::formatv("Waiting to attach to \"{0}\"...",
                                       m_context.Target().GetExecutable().GetFilename())
                             .str());

  {
    // Perform the launch in synchronous mode so that we don't have to worry
    // about process state changes during the launch.
    ScopeSyncMode scope_sync_mode(m_context.Lldb().debugger);

    if (!arguments.attachCommands.empty()) {
      // Run the attach commands, after which we expect the debugger's selected
      // target to contain a valid and stopped process. Otherwise inform the
      // user that their command failed or the debugger is in an unexpected
      // state.
      if (llvm::Error err = RunAttachCommands(arguments.attachCommands))
        return err;

      m_context.Target() = m_context.Lldb().debugger.GetSelectedTarget();

      // Validate the attachCommand results.
      if (!m_context.Target().GetProcess().IsValid())
        return llvm::make_error<dap::DAPError>("attachCommands failed to attach to a process");
    } else if (!arguments.coreFile.empty()) {
      m_context.Target().LoadCore(arguments.coreFile.data(), error);
    } else if (arguments.gdbRemotePort != LLDB_DAP_INVALID_PORT) {
      lldb::SBListener listener = m_context.Lldb().debugger.GetListener();

      // If the user hasn't provided the hostname property, default
      // localhost being used.
      std::string connect_url = llvm::formatv("connect://{0}:", arguments.gdbRemoteHostname);
      connect_url += std::to_string(arguments.gdbRemotePort);
      m_context.Target().ConnectRemote(listener, connect_url.c_str(), "gdb-remote", error);
    } else if (!session) {
      // Attach by pid or process name.
      lldb::SBAttachInfo attach_info;
      if (arguments.pid != LLDB_INVALID_PROCESS_ID)
        attach_info.SetProcessID(arguments.pid);
      else if (!configuration.program.empty())
        attach_info.SetExecutable(configuration.program.data());
      attach_info.SetWaitForLaunch(arguments.waitFor, /*async=*/false);
      m_context.Target().Attach(attach_info, error);
    }

    if (error.Fail())
      return ToError(error);
  }

  // Make sure the process is attached and stopped.
  error = m_context.Execution().WaitForProcessToStop(arguments.configuration.timeout);
  if (error.Fail())
    return ToError(error);

  if (arguments.coreFile.empty() && !m_context.Target().GetProcess().IsValid())
    return llvm::make_error<dap::DAPError>("failed to attach to process");

  RunPostRunCommands();

  return llvm::Error::success();
}

llvm::Error TargetManager::Launch(const dap::protocol::LaunchRequestArguments &arguments) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  if (llvm::Error err = InitializeDebugger())
    return err;

  SetConfiguration(arguments.configuration, /*is_attach=*/false);
  last_launch_request = arguments;

  // This is a hack for loading DWARF in .o files on Mac where the .o files
  // in the debug map of the main executable have relative paths which
  // require the lldb-dap binary to have its working directory set to that
  // relative root for the .o files in order to be able to load debug info.
  if (!configuration.debuggerRoot.empty())
    llvm::sys::fs::set_current_path(configuration.debuggerRoot);

  // Run any initialize LLDB commands the user specified in the launch.json.
  // This is run before target is created, so commands can't do anything with
  // the targets - preRunCommands are run with the target.
  if (llvm::Error err = RunInitCommands())
    return err;

  ConfigureSourceMaps();

  lldb::SBError error;
  lldb::SBTarget target = CreateTarget(error);
  if (error.Fail())
    return ToError(error);

  SetTarget(target);

  // Run any pre run LLDB commands the user specified in the launch.json
  if (llvm::Error err = RunPreRunCommands())
    return err;

  if (llvm::Error err = LaunchProcess(arguments))
    return err;

  RunPostRunCommands();

  return llvm::Error::success();
}

llvm::Error TargetManager::Restart(const std::optional<dap::protocol::RestartArguments> &arguments) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  if (!m_context.Target().GetProcess().IsValid())
    return llvm::make_error<dap::DAPError>("Restart request received but no process was launched.");

  if (arguments) {
    if (std::holds_alternative<dap::protocol::AttachRequestArguments>(arguments->arguments))
      return llvm::make_error<dap::DAPError>("Restarting an AttachRequest is not supported.");
    if (const auto *launch_arguments =
            std::get_if<dap::protocol::LaunchRequestArguments>(&arguments->arguments)) {
      last_launch_request = *launch_arguments;
      // Update the configuration based on the latest copy of the launch
      // arguments.
      SetConfiguration(launch_arguments->configuration, /*is_attach=*/false);
      ConfigureSourceMaps();
    }
  }

  // Keep track of the old PID so when we get a "process exited" event from the
  // killed process we can detect it and not shut down the whole session.
  lldb::SBProcess process = m_context.Target().GetProcess();
  restarting_process_id = process.GetProcessID();

  // Stop the current process if necessary. The logic here is similar to
  // CommandObjectProcessLaunchOrAttach::StopProcessIfNecessary, except that
  // we don't ask the user for confirmation.
  if (process.IsValid()) {
    ScopeSyncMode scope_sync_mode(m_context.Lldb().debugger);
    lldb::StateType state = process.GetState();
    if (state != lldb::eStateConnected) {
      if (lldb::SBError error = process.Kill(); error.Fail())
        return ToError(error);
    }
    // Clear the list of thread ids to avoid sending "thread exited" events
    // for threads of the process we are terminating.
    m_context.Execution().thread_ids.clear();
  }

  // FIXME: Should we run 'preRunCommands'?
  // FIXME: Should we add a 'preRestartCommands'?
  if (llvm::Error error = LaunchProcess(*last_launch_request))
    return error;

  m_context.Execution().SendProcessEvent(core::Launch);

  // This is normally done after receiving a "configuration done" request.
  // Because we're restarting, configuration has already happened so we can
  // continue the process right away.
  if (m_context.Execution().stop_at_entry)
    return m_context.Execution().SendThreadStoppedEvent(/*on_entry=*/true);

  return ToError(m_context.Target().GetProcess().Continue());
}

void TargetManager::SendTerminatedEvent() {
  llvm::call_once(m_terminated_event_flag, [&] {
    RunTerminateCommands();
    m_context.Emit(TerminatedEvent{BuildTerminatedStatisticsJSON(m_context.Target())});
  });
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
