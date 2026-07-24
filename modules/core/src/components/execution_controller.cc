#include "core/components/execution_controller.hpp"

#include <cstdint>
#include <sstream>
#include <utility>

#include <mutex>

#include "core/components/breakpoint.hpp"
#include "core/components/breakpoint_base.hpp"
#include "core/components/breakpoint_manager.hpp"
#include "core/components/data_manager.hpp"
#include "core/components/module_manager.hpp"
#include "core/debug_context.hpp"
#include "dap/dap_error.hpp"
#include "dap/json_utils.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBPlatform.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Threading.h"

namespace core {

namespace protocol = dap::protocol;
using namespace dap;

protocol::Thread CreateThread(lldb::SBThread &thread, lldb::SBFormat &format) {
  std::string name;
  lldb::SBStream stream;
  if (format && thread.GetDescriptionWithFormat(format, stream).Success()) {
    name = stream.GetData();
  } else {
    llvm::StringRef thread_name(thread.GetName());
    llvm::StringRef queue_name(thread.GetQueueName());

    if (!thread_name.empty()) {
      name = thread_name.str();
    } else if (!queue_name.empty()) {
      auto kind = thread.GetQueue().GetKind();
      std::string queue_kind_label = "";
      if (kind == lldb::eQueueKindSerial)
        queue_kind_label = " (serial)";
      else if (kind == lldb::eQueueKindConcurrent)
        queue_kind_label = " (concurrent)";

      name = llvm::formatv("Thread {0} Queue: {1}{2}", thread.GetIndexID(), queue_name, queue_kind_label).str();
    } else {
      name = llvm::formatv("Thread {0}", thread.GetIndexID()).str();
    }
  }
  return protocol::Thread{thread.GetThreadID(), name};
}

std::vector<protocol::Thread> GetThreads(lldb::SBProcess process, lldb::SBFormat &format) {
  lldb::SBMutex lock = process.GetTarget().GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  std::vector<protocol::Thread> threads;

  const uint32_t num_threads = process.GetNumThreads();
  threads.reserve(num_threads);
  for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
    lldb::SBThread thread = process.GetThreadAtIndex(thread_idx);
    threads.emplace_back(CreateThread(thread, format));
  }
  return threads;
}

namespace {

bool ThreadHasStopReason(lldb::SBThread &thread) {
  switch (thread.GetStopReason()) {
  case lldb::eStopReasonTrace:
  case lldb::eStopReasonBreakpoint:
  case lldb::eStopReasonWatchpoint:
  case lldb::eStopReasonSignal:
  case lldb::eStopReasonException:
  case lldb::eStopReasonExec:
  case lldb::eStopReasonFork:
  case lldb::eStopReasonVFork:
  case lldb::eStopReasonVForkDone:
  case lldb::eStopReasonPlanComplete:
  case lldb::eStopReasonInstrumentation:
  case lldb::eStopReasonProcessorTrace:
  case lldb::eStopReasonInterrupt:
    return true;
  case lldb::eStopReasonThreadExiting:
  case lldb::eStopReasonInvalid:
  case lldb::eStopReasonNone:
  case lldb::eStopReasonHistoryBoundary:
    break;
  }
  return false;
}

std::string GetStringValue(const lldb::SBStructuredData &data) {
  if (data.GetType() != lldb::eStructuredDataTypeString)
    return {};
  char buf[1024];
  size_t size = data.GetStringValue(buf, sizeof(buf));
  return std::string(buf, std::min(size, sizeof(buf) - 1));
}

/// Get capabilities based on the configured target.
llvm::DenseSet<protocol::AdapterFeature> GetTargetBasedCapabilities(DebugContext &context) {
  llvm::DenseSet<protocol::AdapterFeature> capabilities;
  if (!context.Target().IsValid())
    return capabilities;

  const llvm::StringRef target_triple = context.Target().GetTriple();
  if (target_triple.starts_with("x86"))
    capabilities.insert(protocol::eAdapterFeatureStepInTargetsRequest);

  // We only support restarting launch requests not attach requests.
  if (context.HasLastLaunchRequest())
    capabilities.insert(protocol::eAdapterFeatureRestartRequest);

  return capabilities;
}

}  // namespace

ExecutionController::ExecutionController(DebugContext &context) : m_context(context) {}

ExecutionController::~ExecutionController() { StopEventHandlers(); }

void ExecutionController::WillContinue() { m_context.Data().variables.Clear(); }

lldb::SBError ExecutionController::WaitForProcessToStop(std::chrono::seconds seconds) {
  lldb::SBError error;
  lldb::SBProcess process = m_context.Target().GetProcess();
  if (!process.IsValid()) {
    error.SetErrorString("invalid process");
    return error;
  }
  auto timeout_time = std::chrono::steady_clock::now() + seconds;
  while (std::chrono::steady_clock::now() < timeout_time) {
    switch (process.GetState()) {
    case lldb::eStateUnloaded:
    case lldb::eStateAttaching:
    case lldb::eStateConnected:
    case lldb::eStateInvalid:
    case lldb::eStateLaunching:
    case lldb::eStateRunning:
    case lldb::eStateStepping:
    case lldb::eStateSuspended:
      break;
    case lldb::eStateDetached:
      error.SetErrorString("process detached during launch or attach");
      return error;
    case lldb::eStateExited:
      error.SetErrorString("process exited during launch or attach");
      return error;
    case lldb::eStateCrashed:
    case lldb::eStateStopped:
      return lldb::SBError();  // Success!
    }
    std::this_thread::sleep_for(std::chrono::microseconds(250));
  }
  error.SetErrorString(llvm::formatv("process failed to stop within {0}", seconds).str().c_str());
  return error;
}

void ExecutionController::SendExtraCapabilities() {
  protocol::Capabilities capabilities = m_context.GetCustomCapabilities();
  llvm::DenseSet<protocol::AdapterFeature> target_capabilities = GetTargetBasedCapabilities(m_context);

  capabilities.supportedFeatures.insert(target_capabilities.begin(), target_capabilities.end());

  protocol::CapabilitiesEventBody body;
  body.capabilities = std::move(capabilities);

  // Only notify the client if supportedFeatures changed.
  if (!body.capabilities.supportedFeatures.empty())
    m_context.Send(protocol::Event{"capabilities", std::move(body)});
}

void ExecutionController::SendProcessEvent(LaunchMethod launch_method) {
  lldb::SBFileSpec exe_fspec = m_context.Target().GetExecutable();
  char exe_path[4096];
  exe_fspec.GetPath(exe_path, sizeof(exe_path));
  llvm::json::Object event(CreateEventObject("process"));
  llvm::json::Object body;
  EmplaceSafeString(body, "name", exe_path);
  const auto pid = m_context.Target().GetProcess().GetProcessID();
  body.try_emplace("systemProcessId", (int64_t)pid);
  body.try_emplace("isLocalProcess", m_context.Target().GetPlatform().IsHost());
  body.try_emplace("pointerSize", m_context.Target().GetAddressByteSize() * 8);
  const char *startMethod = nullptr;
  switch (launch_method) {
  case Launch:
    startMethod = "launch";
    break;
  case Attach:
    startMethod = "attach";
    break;
  case AttachForSuspendedLaunch:
    startMethod = "attachForSuspendedLaunch";
    break;
  }
  body.try_emplace("startMethod", startMethod);
  event.try_emplace("body", std::move(body));
  m_context.SendJSON(llvm::json::Value(std::move(event)));
}

void ExecutionController::SendThreadExitedEvent(lldb::tid_t tid) {
  llvm::json::Object event(CreateEventObject("thread"));
  llvm::json::Object body;
  body.try_emplace("reason", "exited");
  body.try_emplace("threadId", (int64_t)tid);
  event.try_emplace("body", std::move(body));
  m_context.SendJSON(llvm::json::Value(std::move(event)));
}

// Create a "StoppedEvent" body for a LLDB thread object. Mirrors
// debug_service::CreateThreadStopped exactly - see json_utils.cc.
static llvm::json::Value CreateThreadStoppedEvent(DebugContext &context, lldb::SBThread &thread,
                                                  uint32_t stop_id) {
  llvm::json::Object event(CreateEventObject("stopped"));
  llvm::json::Object body;
  switch (thread.GetStopReason()) {
  case lldb::eStopReasonTrace:
  case lldb::eStopReasonPlanComplete:
    body.try_emplace("reason", "step");
    break;
  case lldb::eStopReasonBreakpoint: {
    ExceptionBreakpoint *exc_bp = context.Breakpoints().GetExceptionBPFromStopReason(thread);
    if (exc_bp) {
      body.try_emplace("reason", "exception");
      EmplaceSafeString(body, "description", exc_bp->GetLabel());
    } else {
      body.try_emplace("reason", "breakpoint");
      std::vector<lldb::break_id_t> bp_ids;
      std::ostringstream desc_sstream;
      desc_sstream << "breakpoint";
      for (size_t idx = 0; idx < thread.GetStopReasonDataCount(); idx += 2) {
        lldb::break_id_t bp_id = thread.GetStopReasonDataAtIndex(idx);
        lldb::break_id_t bp_loc_id = thread.GetStopReasonDataAtIndex(idx + 1);
        bp_ids.push_back(bp_id);
        desc_sstream << " " << bp_id << "." << bp_loc_id;
      }
      std::string desc_str = desc_sstream.str();
      body.try_emplace("hitBreakpointIds", llvm::json::Array(bp_ids));
      EmplaceSafeString(body, "description", desc_str);
    }
  } break;
  case lldb::eStopReasonWatchpoint: {
    body.try_emplace("reason", "data breakpoint");
    lldb::break_id_t bp_id = thread.GetStopReasonDataAtIndex(0);
    body.try_emplace("hitBreakpointIds", llvm::json::Array{llvm::json::Value(bp_id)});
    EmplaceSafeString(body, "description", llvm::formatv("data breakpoint {0}", bp_id).str());
  } break;
  case lldb::eStopReasonInstrumentation:
    body.try_emplace("reason", "breakpoint");
    break;
  case lldb::eStopReasonProcessorTrace:
    body.try_emplace("reason", "processor trace");
    break;
  case lldb::eStopReasonHistoryBoundary:
    body.try_emplace("reason", "history boundary");
    break;
  case lldb::eStopReasonSignal:
  case lldb::eStopReasonException:
    body.try_emplace("reason", "exception");
    break;
  case lldb::eStopReasonExec:
    body.try_emplace("reason", "entry");
    break;
  case lldb::eStopReasonFork:
    body.try_emplace("reason", "fork");
    break;
  case lldb::eStopReasonVFork:
    body.try_emplace("reason", "vfork");
    break;
  case lldb::eStopReasonVForkDone:
    body.try_emplace("reason", "vforkdone");
    break;
  case lldb::eStopReasonInterrupt:
    body.try_emplace("reason", "async interrupt");
    break;
  case lldb::eStopReasonThreadExiting:
  case lldb::eStopReasonInvalid:
  case lldb::eStopReasonNone:
    break;
  }
  if (stop_id == 0)
    body["reason"] = "entry";
  const lldb::tid_t tid = thread.GetThreadID();
  body.try_emplace("threadId", (int64_t)tid);
  // If no description has been set, then set it to the default thread
  // stopped description.
  if (!ObjectContainsKey(body, "description")) {
    char description[1024];
    if (thread.GetStopDescription(description, sizeof(description)))
      EmplaceSafeString(body, "description", description);
  }
  // "threadCausedFocus" is used in tests to validate breaking behavior.
  if (tid == context.Execution().focus_tid)
    body.try_emplace("threadCausedFocus", true);
  body.try_emplace("preserveFocusHint", tid != context.Execution().focus_tid);
  body.try_emplace("allThreadsStopped", true);
  event.try_emplace("body", std::move(body));
  return llvm::json::Value(std::move(event));
}

llvm::Error ExecutionController::SendThreadStoppedEvent(bool on_entry) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  lldb::SBProcess process = m_context.Target().GetProcess();
  if (!process.IsValid())
    return llvm::make_error<DAPError>("invalid process");

  lldb::StateType state = process.GetState();
  if (!lldb::SBDebugger::StateIsStoppedState(state))
    return llvm::make_error<NotStoppedError>();

  llvm::DenseSet<lldb::tid_t> old_thread_ids;
  old_thread_ids.swap(thread_ids);
  uint32_t stop_id = on_entry ? 0 : process.GetStopID();
  const uint32_t num_threads = process.GetNumThreads();

  // First make a pass through the threads to see if the focused thread
  // has a stop reason. In case the focus thread doesn't have a stop
  // reason, remember the first thread that has a stop reason so we can
  // set it as the focus thread if below if needed.
  lldb::tid_t first_tid_with_reason = LLDB_INVALID_THREAD_ID;
  uint32_t num_threads_with_reason = 0;
  bool focus_thread_exists = false;
  for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
    lldb::SBThread thread = process.GetThreadAtIndex(thread_idx);
    const lldb::tid_t tid = thread.GetThreadID();
    const bool has_reason = ThreadHasStopReason(thread);
    if (tid == focus_tid) {
      focus_thread_exists = true;
      if (!has_reason)
        focus_tid = LLDB_INVALID_THREAD_ID;
    }
    if (has_reason) {
      ++num_threads_with_reason;
      if (first_tid_with_reason == LLDB_INVALID_THREAD_ID)
        first_tid_with_reason = tid;
    }
  }

  if (!focus_thread_exists || focus_tid == LLDB_INVALID_THREAD_ID)
    focus_tid = first_tid_with_reason;

  // If no threads stopped with a reason, then report the first one so we
  // at least let the UI know we stopped.
  if (num_threads_with_reason == 0) {
    lldb::SBThread thread = process.GetThreadAtIndex(0);
    focus_tid = thread.GetThreadID();
    m_context.SendJSON(CreateThreadStoppedEvent(m_context, thread, stop_id));
  } else {
    for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
      lldb::SBThread thread = process.GetThreadAtIndex(thread_idx);
      thread_ids.insert(thread.GetThreadID());
      if (ThreadHasStopReason(thread))
        m_context.SendJSON(CreateThreadStoppedEvent(m_context, thread, stop_id));
    }
  }

  for (const auto &tid : old_thread_ids) {
    auto end = thread_ids.end();
    auto pos = thread_ids.find(tid);
    if (pos == end)
      SendThreadExitedEvent(tid);
  }

  m_context.RunStopCommands();
  return llvm::Error::success();
}

void ExecutionController::SendStdOutStdErr(lldb::SBProcess &process) {
  constexpr uint64_t kOutputBufferSize = (1u << 12);
  char buffer[kOutputBufferSize];
  size_t count;
  while ((count = process.GetSTDOUT(buffer, sizeof(buffer))) > 0)
    m_context.SendOutput(OutputCategory::Stdout, llvm::StringRef(buffer, count));
  while ((count = process.GetSTDERR(buffer, sizeof(buffer))) > 0)
    m_context.SendOutput(OutputCategory::Stderr, llvm::StringRef(buffer, count));
}

void ExecutionController::SendContinuedEvent() {
  lldb::SBProcess process = m_context.Target().GetProcess();
  if (!process.IsValid())
    return;

  // If the focus thread is not set then we haven't reported any thread
  // status to the client, so nothing to report.
  if (!configuration_done || focus_tid == LLDB_INVALID_THREAD_ID)
    return;

  llvm::json::Object event(CreateEventObject("continued"));
  llvm::json::Object body;
  body.try_emplace("threadId", (int64_t)focus_tid);
  body.try_emplace("allThreadsContinued", true);
  event.try_emplace("body", std::move(body));
  m_context.SendJSON(llvm::json::Value(std::move(event)));
}

void ExecutionController::SendProcessExitedEvent(lldb::SBProcess &process) {
  llvm::json::Object event(CreateEventObject("exited"));
  llvm::json::Object body;
  body.try_emplace("exitCode", (int64_t)process.GetExitStatus());
  event.try_emplace("body", std::move(body));
  m_context.SendJSON(llvm::json::Value(std::move(event)));
}

void ExecutionController::SendInvalidatedEvent(llvm::ArrayRef<protocol::InvalidatedEventBody::Area> areas,
                                               lldb::tid_t tid) {
  if (!m_context.ClientFeatureEnabled(protocol::eClientFeatureInvalidatedEvent))
    return;
  protocol::InvalidatedEventBody body;
  body.areas = areas;

  if (tid != LLDB_INVALID_THREAD_ID)
    body.threadId = tid;

  m_context.Send(protocol::Event{"invalidated", std::move(body)});
}

void ExecutionController::SendMemoryEvent(lldb::SBValue variable) {
  if (!m_context.ClientFeatureEnabled(protocol::eClientFeatureMemoryEvent))
    return;
  protocol::MemoryEventBody body;
  body.memoryReference = variable.GetLoadAddress();
  body.count = variable.GetByteSize();
  if (body.memoryReference == LLDB_INVALID_ADDRESS)
    return;
  m_context.Send(protocol::Event{"memory", std::move(body)});
}

// Event handler functions called by EventThreadMain. Adapted from
// upstream: there, these look up "which DAP session owns this event" via
// DAPSessionManager::FindDAP(...), because lldb-dap supports multiple
// sessions sharing one debugger/process. This project has exactly one
// session per process, so each handler just uses `m_context` directly.

void ExecutionController::HandleProcessEvent(const lldb::SBEvent &event, bool &process_exited) {
  lldb::SBProcess process = lldb::SBProcess::GetProcessFromEvent(event);

  const uint32_t event_mask = event.GetType();

  if (event_mask & lldb::SBProcess::eBroadcastBitStateChanged) {
    auto state = lldb::SBProcess::GetStateFromEvent(event);
    switch (state) {
    case lldb::eStateConnected:
    case lldb::eStateDetached:
    case lldb::eStateInvalid:
    case lldb::eStateUnloaded:
      break;
    case lldb::eStateAttaching:
    case lldb::eStateCrashed:
    case lldb::eStateLaunching:
    case lldb::eStateStopped:
    case lldb::eStateSuspended:
      // Only report a stopped event if the process was not automatically
      // restarted.
      if (!lldb::SBProcess::GetRestartedFromEvent(event)) {
        SendStdOutStdErr(process);
        if (llvm::Error err = SendThreadStoppedEvent())
          m_context.LogDiagnostic(
              llvm::formatv("({1}) reporting thread stopped: {0}", llvm::toString(std::move(err)),
                            m_context.ClientName())
                  .str());
      }
      break;
    case lldb::eStateRunning:
    case lldb::eStateStepping:
      WillContinue();
      SendContinuedEvent();
      break;
    case lldb::eStateExited: {
      lldb::SBStream stream;
      process.GetStatus(stream);
      m_context.SendOutput(OutputCategory::Console, stream.GetData());

      // When restarting, we can get an "exited" event for the process we
      // just killed with the old PID, or even with no PID. In that case
      // we don't have to terminate the session.
      if (process.GetProcessID() == LLDB_INVALID_PROCESS_ID ||
          process.GetProcessID() == m_context.RestartingProcessId()) {
        m_context.SetRestartingProcessId(LLDB_INVALID_PROCESS_ID);
      } else {
        m_context.RunExitCommands();
        SendProcessExitedEvent(process);
        // SendTerminatedEvent is a TargetManager-domain method (runs
        // RunTerminateCommands, builds the debug-info-size summary) - not
        // carved yet, reached via the same DebugService reference every
        // handler still has (see DebugContext::Context() and the
        // terminated-event seam wired in DebugServiceSessionImpl).
        m_context.SendTerminatedEvent();
        process_exited = true;
      }
      break;
    }
    }
  } else if ((event_mask & lldb::SBProcess::eBroadcastBitSTDOUT) ||
            (event_mask & lldb::SBProcess::eBroadcastBitSTDERR)) {
    SendStdOutStdErr(process);
  }
}

void ExecutionController::HandleTargetEvent(const lldb::SBEvent &event) {
  lldb::SBTarget target = lldb::SBTarget::GetTargetFromEvent(event);

  const uint32_t event_mask = event.GetType();
  if (event_mask & lldb::SBTarget::eBroadcastBitModulesLoaded || event_mask & lldb::SBTarget::eBroadcastBitModulesUnloaded ||
      event_mask & lldb::SBTarget::eBroadcastBitSymbolsLoaded || event_mask & lldb::SBTarget::eBroadcastBitSymbolsChanged) {
    const uint32_t num_modules = lldb::SBTarget::GetNumModulesFromEvent(event);
    const bool remove_module = event_mask & lldb::SBTarget::eBroadcastBitModulesUnloaded;

    // NOTE: Both mutexes must be acquired to prevent deadlock when handling
    // `modules_request`, which also requires both locks.
    ModuleManager &modules = m_context.Modules();
    lldb::SBMutex api_mutex = m_context.GetAPIMutex();
    const std::scoped_lock<lldb::SBMutex, std::mutex> guard(api_mutex, modules.modules_mutex);
    for (uint32_t i = 0; i < num_modules; ++i) {
      lldb::SBModule module = lldb::SBTarget::GetModuleAtIndexFromEvent(i, event);

      std::optional<protocol::Module> p_module = modules.CreateModuleDescription(module, remove_module);
      if (!p_module)
        continue;

      llvm::StringRef module_id = p_module->id;

      const bool module_exists = modules.modules.contains(module_id);
      if (remove_module && module_exists) {
        modules.modules.erase(module_id);
        m_context.Send(protocol::Event{
            "module", protocol::ModuleEventBody{std::move(p_module).value(), protocol::ModuleEventBody::eReasonRemoved}});
      } else if (module_exists) {
        m_context.Send(protocol::Event{
            "module", protocol::ModuleEventBody{std::move(p_module).value(), protocol::ModuleEventBody::eReasonChanged}});
      } else if (!remove_module) {
        modules.modules.insert(module_id);
        m_context.Send(protocol::Event{
            "module", protocol::ModuleEventBody{std::move(p_module).value(), protocol::ModuleEventBody::eReasonNew}});
      }
    }
  } else if (event_mask & lldb::SBTarget::eBroadcastBitNewTargetCreated) {
    // For NewTargetCreated events, GetTargetFromEvent returns the parent
    // target, and GetCreatedTargetFromEvent returns the newly created
    // target. This is a multi-target handoff feature (a child target gets
    // its own DAP session) that doesn't apply to this project's
    // single-session model - upstream sends a "startDebugging" reverse
    // request here to hand the new target to a fresh session. That needs
    // Orchestrator-level reverse-request tracking (inflight_reverse_requests)
    // this project's single-session DebugService still owns and core
    // deliberately doesn't reach for - and this project's architecture
    // never creates a second target, so the branch is inert in practice.
    // Left as a diagnostic-only no-op (matches upstream's own guard
    // clause below it) rather than wiring a reverse-request bus event for
    // a code path that cannot fire here.
    lldb::SBTarget created_target = lldb::SBTarget::GetCreatedTargetFromEvent(event);
    if (!target.IsValid() || !created_target.IsValid()) {
      m_context.LogDiagnostic("Received NewTargetCreated event but parent or created target is invalid");
      return;
    }
    m_context.LogDiagnostic("Received NewTargetCreated event - multi-target handoff is not supported");
  }
}

void ExecutionController::HandleBreakpointEvent(const lldb::SBEvent &event) {
  const uint32_t event_mask = event.GetType();
  if (!(event_mask & lldb::SBTarget::eBroadcastBitBreakpointChanged))
    return;

  lldb::SBBreakpoint bp = lldb::SBBreakpoint::GetBreakpointFromEvent(event);
  if (!bp.IsValid())
    return;

  auto event_type = lldb::SBBreakpoint::GetBreakpointEventTypeFromEvent(event);
  auto breakpoint = core::Breakpoint(m_context, bp);
  // If the breakpoint was set through DAP, it will have the
  // core::BreakpointBase::kDAPBreakpointLabel. Regardless of whether
  // locations were added, removed, or resolved, the breakpoint isn't going
  // away and the reason is always "changed".
  if ((event_type & lldb::eBreakpointEventTypeLocationsAdded || event_type & lldb::eBreakpointEventTypeLocationsRemoved ||
       event_type & lldb::eBreakpointEventTypeLocationsResolved) &&
      breakpoint.MatchesName(core::BreakpointBase::kDAPBreakpointLabel)) {
    protocol::Breakpoint protocol_bp = breakpoint.ToProtocolBreakpoint();

    // "source" is not needed here, unless we add adapter data to be saved
    // by the client.
    if (protocol_bp.source && !protocol_bp.source->adapterData)
      protocol_bp.source = std::nullopt;

    llvm::json::Object body;
    body.try_emplace("breakpoint", protocol_bp);
    body.try_emplace("reason", "changed");

    llvm::json::Object bp_event = CreateEventObject("breakpoint");
    bp_event.try_emplace("body", std::move(body));

    m_context.SendJSON(llvm::json::Value(std::move(bp_event)));
  }
}

void ExecutionController::HandleThreadEvent(const lldb::SBEvent &event) {
  uint32_t event_type = event.GetType();

  if (!(event_type & lldb::SBThread::eBroadcastBitStackChanged))
    return;

  lldb::SBThread thread = lldb::SBThread::GetThreadFromEvent(event);
  if (!thread.IsValid())
    return;

  SendInvalidatedEvent({protocol::InvalidatedEventBody::eAreaStacks}, thread.GetThreadID());
}

void ExecutionController::HandleDiagnosticEvent(const lldb::SBEvent &event) {
  lldb::SBStructuredData data = lldb::SBDebugger::GetDiagnosticFromEvent(event);
  if (!data.IsValid())
    return;

  std::string type = GetStringValue(data.GetValueForKey("type"));
  std::string message = GetStringValue(data.GetValueForKey("message"));
  m_context.SendOutput(OutputCategory::Important, llvm::formatv("{0}: {1}", type, message).str());
}

// All events from the debugger, target, process, thread and frames for
// this session are received in this function, which runs in its own
// thread (started from ExecutionController::StartEventThread). Unlike
// upstream's version, this loop is scoped to exactly one session - no
// multi-session dispatch.
void ExecutionController::EventThreadMain() {
  std::string thread_name = llvm::formatv("trailer-dap.event_handler.{0}", m_context.ClientName());
  if (thread_name.length() > llvm::get_max_thread_name_length())
    thread_name = "trailer-dap.evt";
  llvm::set_thread_name(thread_name);

  lldb::SBListener listener = m_context.Lldb().debugger.GetListener();
  broadcaster.AddListener(listener, eBroadcastBitStopEventThread);
  m_context.Lldb().debugger.GetBroadcaster().AddListener(listener, lldb::eBroadcastBitError | lldb::eBroadcastBitWarning);

  // Listen for thread events.
  listener.StartListeningForEventClass(m_context.Lldb().debugger, lldb::SBThread::GetBroadcasterClassName(),
                                       lldb::SBThread::eBroadcastBitStackChanged);

  lldb::SBEvent event;
  bool done = false;
  while (!done) {
    if (!listener.WaitForEvent(UINT32_MAX, event))
      continue;

    const uint32_t event_mask = event.GetType();
    if (lldb::SBProcess::EventIsProcessEvent(event)) {
      HandleProcessEvent(event, /*process_exited=*/done);
    } else if (lldb::SBTarget::EventIsTargetEvent(event)) {
      HandleTargetEvent(event);
    } else if (lldb::SBBreakpoint::EventIsBreakpointEvent(event)) {
      HandleBreakpointEvent(event);
    } else if (lldb::SBThread::EventIsThreadEvent(event)) {
      HandleThreadEvent(event);
    } else if (event_mask & lldb::eBroadcastBitError || event_mask & lldb::eBroadcastBitWarning) {
      HandleDiagnosticEvent(event);
    } else if (event.BroadcasterMatchesRef(broadcaster)) {
      if (event_mask & eBroadcastBitStopEventThread)
        done = true;
    }
  }
}

void ExecutionController::StartEventThread() {
  // std::ref: EventThreadMain takes ExecutionController& - std::thread
  // otherwise decay-copies its arguments, which would try (and fail, it's
  // non-copyable) to copy *this into thread-local storage instead of
  // referencing it.
  m_event_thread = std::thread(&ExecutionController::EventThreadMain, this);
}

void ExecutionController::StopEventHandlers() {
  if (m_event_thread.joinable()) {
    broadcaster.BroadcastEventByType(eBroadcastBitStopEventThread);
    m_event_thread.join();
  }
}

}  // namespace core
