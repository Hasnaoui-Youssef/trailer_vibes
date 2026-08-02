#include "core/components/target_manager.hpp"

#include <expected>
#include <utility>
#include <vector>

#include "core/components/device_manager.hpp"
#include "core/components/disassembly_manager.hpp"
#include "core/components/execution_controller.hpp"
#include "core/components/memory_manager.hpp"
#include "core/components/trace_manager.hpp"
#include "core/components/watch_manager.hpp"
#include "core/debug_context.hpp"
#include "core/lldb_utils.hpp"
#include "dap/dap_error.hpp"
#include "lldb/API/SBAttachInfo.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBFile.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBProcess.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "openocd_provider/openocd_config.hpp"

namespace core {

namespace protocol = dap::protocol;

namespace {

llvm::Error CreateRunLLDBCommandsErrorMessage(llvm::StringRef category) {
  return llvm::createStringError(
      llvm::inconvertibleErrorCode(),
      llvm::formatv("Failed to run {0} commands. See the Debug Console for more details.", category).str());
}

providers::OpenOcdConfig ToOpenOcdConfig(const protocol::OpenOcdConfiguration &config) {
  providers::OpenOcdConfig result;
  result.script_search_dirs = config.scriptSearchDirs;
  result.config_files = config.configFiles;
  result.raw_commands = config.rawCommands;
  result.log_file_path = config.logFile;
  result.debug_level = config.debugLevel;
  result.gdb_port = config.gdbPort;
  result.tcl_port = config.tclPort;
  result.telnet_port = config.telnetPort;
  return result;
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

  m_context.Device().Configure(config.deviceName, config.svdPath);
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

llvm::Error TargetManager::ConfigureHardwareBreakpointRequirement() {
  std::string command = llvm::formatv("settings set target.require-hardware-breakpoint {0}",
                                       configuration.requireHardwareBreakpoints.value_or(true) ? "true" : "false")
                             .str();
  if (!RunLLDBCommands("Setting hardware breakpoint requirement:", {command}))
    return CreateRunLLDBCommandsErrorMessage("hardware breakpoint requirement");
  return llvm::Error::success();
}

lldb::SBTarget TargetManager::CreateTarget(lldb::SBError &error) {
  return m_context.Lldb().debugger.CreateTarget(
      /*filename=*/configuration.program.data(),
      /*target_triple=*/configuration.targetTriple.data(),
      /*platform_name=*/configuration.platformName.data(),
      /*add_dependent_modules=*/true, error);
}

void TargetManager::SetTarget(lldb::SBTarget target) {
  m_context.Disassembly().InvalidateProgram();
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
  // TraceManager's destructor synchronizes with the OpenOCD capture
  // callback, so it must go before OpenOCD itself is torn down. Same for
  // any live memory watch worker still reading through it.
  m_context.ShutdownTrace();
  m_context.Watch().StopAll();
  m_context.Device().StopAllPeripheralWatches();
  // Only after LLDB is done talking to it over gdb-remote (the kill/detach
  // above, and the event thread it could still trigger) - shutting OpenOCD
  // down any earlier pulls the connection out from under that traffic.
  m_context.ShutdownOpenOcd();
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

llvm::Error TargetManager::ConnectToGdbRemote(llvm::StringRef hostname, llvm::StringRef port,
                                              std::chrono::seconds timeout) {
  lldb::SBError error;
  {
    // Perform the connect in synchronous mode so that we don't have to worry
    // about process state changes while it's in progress.
    ScopeSyncMode scope_sync_mode(m_context.Lldb().debugger);
    lldb::SBListener listener = m_context.Lldb().debugger.GetListener();
    std::string connect_url = llvm::formatv("connect://{0}:{1}", hostname, port).str();
    m_context.Target().ConnectRemote(listener, connect_url.c_str(), "gdb-remote", error);
  }
  if (error.Fail())
    return ToError(error);

  error = m_context.Execution().WaitForProcessToStop(timeout);
  return ToError(error);
}

llvm::Error TargetManager::Attach(const dap::protocol::AttachRequestArguments &arguments) {
  return m_context.WithTarget([&]() -> llvm::Error {

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
    if (llvm::Error err = ConfigureHardwareBreakpointRequirement())
      return err;

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

    if (arguments.gdbRemotePort != LLDB_DAP_INVALID_PORT) {
      if (llvm::Error err = ConnectToGdbRemote(arguments.gdbRemoteHostname, std::to_string(arguments.gdbRemotePort),
                                                arguments.configuration.timeout))
        return err;
    } else {
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

      // Make sure the process is attached and stopped.
      error = m_context.Execution().WaitForProcessToStop(arguments.configuration.timeout);
      if (error.Fail())
        return ToError(error);
    }

    if (arguments.coreFile.empty() && !m_context.Target().GetProcess().IsValid())
      return llvm::make_error<dap::DAPError>("failed to attach to process");

    RunPostRunCommands();

    return llvm::Error::success();
  });
}

llvm::Error TargetManager::Launch(const dap::protocol::LaunchRequestArguments &arguments) {
  return m_context.WithTarget([&]() -> llvm::Error {
    if (llvm::Error err = InitializeDebugger())
      return err;

    SetConfiguration(arguments.configuration, /*is_attach=*/false);
    last_launch_request = arguments;

    if (llvm::Error err = m_context.CreateOpenOcd(ToOpenOcdConfig(arguments.openocd)))
      return err;
    m_context.Memory().SetStrategy(std::make_unique<OpenOcdMemoryStrategy>(*m_context.OpenOcd()));

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
    if (llvm::Error err = ConfigureHardwareBreakpointRequirement())
      return err;

    lldb::SBError error;
    lldb::SBTarget target = CreateTarget(error);
    if (error.Fail())
      return ToError(error);

    SetTarget(target);

    // Run any pre run LLDB commands the user specified in the launch.json
    if (llvm::Error err = RunPreRunCommands())
      return err;

    if (llvm::Error err = ConnectToGdbRemote("127.0.0.1", arguments.openocd.gdbPort, arguments.configuration.timeout))
      return err;

    // Trace components come from the sourced OpenOCD config, not every board
    // has them - absence is not a Launch failure, just no trace this session.
    if (llvm::Error err = m_context.CreateTrace())
      m_context.SendOutput(OutputCategory::Console, "trace unavailable: " + llvm::toString(std::move(err)));
    m_context.Emit(TraceStatusEvent{m_context.TraceStatus()});

    RunPostRunCommands();

    return llvm::Error::success();
  });
}

llvm::Error TargetManager::Restart(const std::optional<dap::protocol::RestartArguments> &arguments) {
  return m_context.WithTarget([&]() -> llvm::Error {

    if (!m_context.Target().GetProcess().IsValid())
      return llvm::make_error<dap::DAPError>("Restart request received but no process was launched.");

    if (!m_context.OpenOcd())
      return llvm::make_error<dap::DAPError>("Restart is only supported for launch sessions.");

    if (arguments) {
      if (std::holds_alternative<dap::protocol::AttachRequestArguments>(arguments->arguments))
        return llvm::make_error<dap::DAPError>("Restarting an AttachRequest is not supported.");
      if (const auto *launch_arguments =
              std::get_if<dap::protocol::LaunchRequestArguments>(&arguments->arguments)) {
        last_launch_request = *launch_arguments;
        SetConfiguration(launch_arguments->configuration, /*is_attach=*/false);
        ConfigureSourceMaps();
        if (llvm::Error err = ConfigureHardwareBreakpointRequirement())
          return err;
      }
    }

    lldb::SBProcess process = m_context.Target().GetProcess();
    const bool was_running = !lldb::SBDebugger::StateIsStoppedState(process.GetState());
    const bool trace_was_enabled = m_context.Trace() && m_context.Trace()->enabled();

    m_context.Emit(ResetEvent{ResetPhase::Started});
    m_context.Execution().SendContinuedEvent();
    m_context.Execution().thread_ids.clear();
    m_context.Execution().reset_pending_stop = true;
    m_context.Execution().reset_resume_after_stop = !m_context.Execution().stop_at_entry;

    m_context.ShutdownTrace();

    if (std::expected<void, std::string> result = m_context.OpenOcd()->RunTclCommand("reset halt"); !result) {
      m_context.Execution().reset_pending_stop = false;
      m_context.Execution().reset_resume_after_stop = false;
      return llvm::make_error<dap::DAPError>(result.error());
    }

    m_context.Disassembly().InvalidateProgram();
    if (llvm::Error err = m_context.CreateTrace()) {
      m_context.SendOutput(OutputCategory::Console, "trace unavailable: " + llvm::toString(std::move(err)));
      m_context.Emit(TraceStatusEvent{m_context.TraceStatus()});
    } else if (trace_was_enabled) {
      if (llvm::Error err = m_context.Trace()->Enable()) {
        m_context.SendOutput(OutputCategory::Console, "trace re-arm failed: " + llvm::toString(std::move(err)));
        m_context.Emit(TraceStatusEvent{m_context.TraceStatus()});
      }
    } else {
      m_context.Emit(TraceStatusEvent{m_context.TraceStatus()});
    }

    // A halt while already running produces a genuine stop reply on its own
    // (frontend_state was TARGET_RUNNING) - no resync needed.
    if (was_running)
      return llvm::Error::success();

    if (std::expected<void, std::string> result = m_context.OpenOcd()->RunTclCommand("gdb_resync"); !result) {
      m_context.Execution().reset_pending_stop = false;
      m_context.Execution().reset_resume_after_stop = false;
      return llvm::make_error<dap::DAPError>(result.error());
    }

    lldb::SBThread thread = process.GetThreadAtIndex(0);
    lldb::SBError step_error;
    thread.StepInstruction(/*step_over=*/false, step_error);
    return ToError(step_error);
  });
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

  // Disable LLDB's parallel module loading: it spawns its own background
  // worker threads inside CreateTarget() that touch per-module locks
  // independently of WithTarget()'s outer API mutex - confirmed via
  // ThreadSanitizer against real hardware to produce a genuine lock-order-
  // inversion with our own SB API calls. Embedded firmware images are small
  // enough that the parallelism buys nothing worth that risk.
  {
    lldb::SBCommandReturnObject result;
    debugger.GetCommandInterpreter().HandleCommand("settings set target.parallel-module-load false", result);
  }

  // Prevents a user ~/.lldbinit from undoing breakpoint.cc's inlined-call-site
  // resolution (GetOutermostInlinedCallSite()).
  {
    lldb::SBCommandReturnObject result;
    debugger.GetCommandInterpreter().HandleCommand("settings set target.inline-breakpoint-strategy always", result);
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
