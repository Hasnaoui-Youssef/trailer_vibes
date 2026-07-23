//===-- EventHelper.h -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "debug_service/event_helper.hpp"
#include "debug_service/breakpoint.hpp"
#include "debug_service/breakpoint_base.hpp"
#include "debug_service/debug_service.hpp"
#include "debug_service/dap_error.hpp"
#include "debug_service/dap_log.hpp"
#include "debug_service/response_handler.hpp"
#include "debug_service/json_utils.hpp"
#include "debug_service/lldb_utils.hpp"
#include "dap/protocol/protocol_events.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "debug_service/protocol_utils.hpp"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBPlatform.h"
#include "lldb/API/SBStream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Threading.h"
#include <mutex>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#endif

using namespace llvm;

namespace dap::debug_service {

static void SendThreadExitedEvent(DebugService &dap, lldb::tid_t tid) {
  llvm::json::Object event(CreateEventObject("thread"));
  llvm::json::Object body;
  body.try_emplace("reason", "exited");
  body.try_emplace("threadId", (int64_t)tid);
  event.try_emplace("body", std::move(body));
  dap.SendJSON(llvm::json::Value(std::move(event)));
}

/// Get capabilities based on the configured target.
static llvm::DenseSet<AdapterFeature> GetTargetBasedCapabilities(DebugService &dap) {
  llvm::DenseSet<AdapterFeature> capabilities;
  if (!dap.target.IsValid())
    return capabilities;

  const llvm::StringRef target_triple = dap.target.GetTriple();
  if (target_triple.starts_with("x86"))
    capabilities.insert(protocol::eAdapterFeatureStepInTargetsRequest);

  // We only support restarting launch requests not attach requests.
  if (dap.last_launch_request)
    capabilities.insert(protocol::eAdapterFeatureRestartRequest);

  return capabilities;
}

void SendExtraCapabilities(DebugService &dap) {
  protocol::Capabilities capabilities = dap.GetCustomCapabilities();
  llvm::DenseSet<AdapterFeature> target_capabilities =
      GetTargetBasedCapabilities(dap);

  capabilities.supportedFeatures.insert(target_capabilities.begin(),
                                        target_capabilities.end());

  protocol::CapabilitiesEventBody body;
  body.capabilities = std::move(capabilities);

  // Only notify the client if supportedFeatures changed.
  if (!body.capabilities.supportedFeatures.empty())
    dap.Send(protocol::Event{"capabilities", std::move(body)});
}

// "ProcessEvent": {
//   "allOf": [
//     { "$ref": "#/definitions/Event" },
//     {
//       "type": "object",
//       "description": "The event indicates that the debugger has begun
//       debugging a new process. Either one that it has launched, or one that
//       it has attached to.", "properties": {
//         "event": {
//           "type": "string",
//           "enum": [ "process" ]
//         },
//         "body": {
//           "type": "object",
//           "properties": {
//             "name": {
//               "type": "string",
//               "description": "The logical name of the process. This is
//               usually the full path to process's executable file. Example:
//               /home/example/myproj/program.js."
//             },
//             "systemProcessId": {
//               "type": "integer",
//               "description": "The process ID of the debugged process, as
//               assigned by the operating system. This property should be
//               omitted for logical processes that do not map to operating
//               system processes on the machine."
//             },
//             "isLocalProcess": {
//               "type": "boolean",
//               "description": "If true, the process is running on the same
//               computer as the debug adapter."
//             },
//             "startMethod": {
//               "type": "string",
//               "enum": [ "launch", "attach", "attachForSuspendedLaunch" ],
//               "description": "Describes how the debug engine started
//               debugging this process.", "enumDescriptions": [
//                 "Process was launched under the debugger.",
//                 "Debugger attached to an existing process.",
//                 "A project launcher component has launched a new process in a
//                 suspended state and then asked the debugger to attach."
//               ]
//             },
//             "pointerSize": {
//               "type": "integer",
//               "description": "The size of a pointer or address for this
//               process, in bits. This value may be used by clients when
//               formatting addresses for display."
//             }
//           },
//           "required": [ "name" ]
//         }
//       },
//       "required": [ "event", "body" ]
//     }
//   ]
// },
void SendProcessEvent(DebugService &dap, LaunchMethod launch_method) {
  lldb::SBFileSpec exe_fspec = dap.target.GetExecutable();
  char exe_path[PATH_MAX];
  exe_fspec.GetPath(exe_path, sizeof(exe_path));
  llvm::json::Object event(CreateEventObject("process"));
  llvm::json::Object body;
  EmplaceSafeString(body, "name", exe_path);
  const auto pid = dap.target.GetProcess().GetProcessID();
  body.try_emplace("systemProcessId", (int64_t)pid);
  body.try_emplace("isLocalProcess", dap.target.GetPlatform().IsHost());
  body.try_emplace("pointerSize", dap.target.GetAddressByteSize() * 8);
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
  dap.SendJSON(llvm::json::Value(std::move(event)));
}

// Send a thread stopped event for all threads as long as the process
// is stopped.
llvm::Error SendThreadStoppedEvent(DebugService &dap, bool on_entry) {
  lldb::SBMutex lock = dap.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  lldb::SBProcess process = dap.target.GetProcess();
  if (!process.IsValid())
    return make_error<DAPError>("invalid process");

  lldb::StateType state = process.GetState();
  if (!lldb::SBDebugger::StateIsStoppedState(state))
    return make_error<NotStoppedError>();

  llvm::DenseSet<lldb::tid_t> old_thread_ids;
  old_thread_ids.swap(dap.thread_ids);
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
    // If the focus thread doesn't have a stop reason, clear the thread ID
    if (tid == dap.focus_tid) {
      focus_thread_exists = true;
      if (!has_reason)
        dap.focus_tid = LLDB_INVALID_THREAD_ID;
    }
    if (has_reason) {
      ++num_threads_with_reason;
      if (first_tid_with_reason == LLDB_INVALID_THREAD_ID)
        first_tid_with_reason = tid;
    }
  }

  // We will have cleared dap.focus_tid if the focus thread doesn't have
  // a stop reason, so if it was cleared, or wasn't set, or doesn't exist,
  // then set the focus thread to the first thread with a stop reason.
  if (!focus_thread_exists || dap.focus_tid == LLDB_INVALID_THREAD_ID)
    dap.focus_tid = first_tid_with_reason;

  // If no threads stopped with a reason, then report the first one so
  // we at least let the UI know we stopped.
  if (num_threads_with_reason == 0) {
    lldb::SBThread thread = process.GetThreadAtIndex(0);
    dap.focus_tid = thread.GetThreadID();
    dap.SendJSON(CreateThreadStopped(dap, thread, stop_id));
  } else {
    for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
      lldb::SBThread thread = process.GetThreadAtIndex(thread_idx);
      dap.thread_ids.insert(thread.GetThreadID());
      if (ThreadHasStopReason(thread)) {
        dap.SendJSON(CreateThreadStopped(dap, thread, stop_id));
      }
    }
  }

  for (const auto &tid : old_thread_ids) {
    auto end = dap.thread_ids.end();
    auto pos = dap.thread_ids.find(tid);
    if (pos == end)
      SendThreadExitedEvent(dap, tid);
  }

  dap.RunStopCommands();
  return Error::success();
}

// Send a "terminated" event to indicate the process is done being
// debugged.
void SendTerminatedEvent(DebugService &dap) { dap.SendTerminatedEvent(); }

// Grab any STDOUT and STDERR from the process and send it up to VS Code
// via an "output" event to the "stdout" and "stderr" categories.
void SendStdOutStdErr(DebugService &dap, lldb::SBProcess &process) {
  char buffer[OutputBufferSize];
  size_t count;
  while ((count = process.GetSTDOUT(buffer, sizeof(buffer))) > 0)
    dap.SendOutput(OutputType::Stdout, llvm::StringRef(buffer, count));
  while ((count = process.GetSTDERR(buffer, sizeof(buffer))) > 0)
    dap.SendOutput(OutputType::Stderr, llvm::StringRef(buffer, count));
}

// Send a "continued" event to indicate the process is in the running state.
void SendContinuedEvent(DebugService &dap) {
  lldb::SBProcess process = dap.target.GetProcess();
  if (!process.IsValid()) {
    return;
  }

  // If the focus thread is not set then we haven't reported any thread status
  // to the client, so nothing to report.
  if (!dap.configuration_done || dap.focus_tid == LLDB_INVALID_THREAD_ID) {
    return;
  }

  llvm::json::Object event(CreateEventObject("continued"));
  llvm::json::Object body;
  body.try_emplace("threadId", (int64_t)dap.focus_tid);
  body.try_emplace("allThreadsContinued", true);
  event.try_emplace("body", std::move(body));
  dap.SendJSON(llvm::json::Value(std::move(event)));
}

// Send a "exited" event to indicate the process has exited.
void SendProcessExitedEvent(DebugService &dap, lldb::SBProcess &process) {
  llvm::json::Object event(CreateEventObject("exited"));
  llvm::json::Object body;
  body.try_emplace("exitCode", (int64_t)process.GetExitStatus());
  event.try_emplace("body", std::move(body));
  dap.SendJSON(llvm::json::Value(std::move(event)));
}

void SendInvalidatedEvent(
    DebugService &dap, llvm::ArrayRef<protocol::InvalidatedEventBody::Area> areas,
    lldb::tid_t tid) {
  if (!dap.clientFeatures.contains(protocol::eClientFeatureInvalidatedEvent))
    return;
  protocol::InvalidatedEventBody body;
  body.areas = areas;

  if (tid != LLDB_INVALID_THREAD_ID)
    body.threadId = tid;

  dap.Send(protocol::Event{"invalidated", std::move(body)});
}

void SendMemoryEvent(DebugService &dap, lldb::SBValue variable) {
  if (!dap.clientFeatures.contains(protocol::eClientFeatureMemoryEvent))
    return;
  protocol::MemoryEventBody body;
  body.memoryReference = variable.GetLoadAddress();
  body.count = variable.GetByteSize();
  if (body.memoryReference == LLDB_INVALID_ADDRESS)
    return;
  dap.Send(protocol::Event{"memory", std::move(body)});
}

// Event handler functions called by EventThread. Adapted from upstream:
// there, these look up "which DAP session owns this event" via
// DAPSessionManager::FindDAP(...), because lldb-dap supports multiple
// sessions sharing one debugger/process. This project has exactly one
// DebugService per process (see project-dap-layer-fork-strategy memory on
// why DAPSessionManager itself isn't forked), so each handler just takes
// the owning DebugService directly - simpler than upstream, not a
// workaround.

static void HandleProcessEvent(const lldb::SBEvent &event, bool &process_exited, DebugService &dap) {
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
        SendStdOutStdErr(dap, process);
        if (llvm::Error err = SendThreadStoppedEvent(dap))
          DAP_LOG_ERROR(dap.log, std::move(err), "({1}) reporting thread stopped: {0}", dap.GetClientName());
      }
      break;
    case lldb::eStateRunning:
    case lldb::eStateStepping:
      dap.WillContinue();
      SendContinuedEvent(dap);
      break;
    case lldb::eStateExited: {
      lldb::SBStream stream;
      process.GetStatus(stream);
      dap.SendOutput(OutputType::Console, stream.GetData());

      // When restarting, we can get an "exited" event for the process we
      // just killed with the old PID, or even with no PID. In that case we
      // don't have to terminate the session.
      if (process.GetProcessID() == LLDB_INVALID_PROCESS_ID ||
          process.GetProcessID() == dap.restarting_process_id) {
        dap.restarting_process_id = LLDB_INVALID_PROCESS_ID;
      } else {
        dap.RunExitCommands();
        SendProcessExitedEvent(dap, process);
        dap.SendTerminatedEvent();
        process_exited = true;
      }
      break;
    }
    }
  } else if ((event_mask & lldb::SBProcess::eBroadcastBitSTDOUT) || (event_mask & lldb::SBProcess::eBroadcastBitSTDERR)) {
    SendStdOutStdErr(dap, process);
  }
}

static void HandleTargetEvent(const lldb::SBEvent &event, DebugService &dap) {
  lldb::SBTarget target = lldb::SBTarget::GetTargetFromEvent(event);

  const uint32_t event_mask = event.GetType();
  if (event_mask & lldb::SBTarget::eBroadcastBitModulesLoaded || event_mask & lldb::SBTarget::eBroadcastBitModulesUnloaded ||
      event_mask & lldb::SBTarget::eBroadcastBitSymbolsLoaded || event_mask & lldb::SBTarget::eBroadcastBitSymbolsChanged) {
    const uint32_t num_modules = lldb::SBTarget::GetNumModulesFromEvent(event);
    const bool remove_module = event_mask & lldb::SBTarget::eBroadcastBitModulesUnloaded;

    // NOTE: Both mutexes must be acquired to prevent deadlock when handling
    // `modules_request`, which also requires both locks.
    lldb::SBMutex api_mutex = dap.GetAPIMutex();
    const std::scoped_lock<lldb::SBMutex, std::mutex> guard(api_mutex, dap.modules_mutex);
    for (uint32_t i = 0; i < num_modules; ++i) {
      lldb::SBModule module = lldb::SBTarget::GetModuleAtIndexFromEvent(i, event);

      std::optional<protocol::Module> p_module = CreateModule(dap.target, module, remove_module);
      if (!p_module)
        continue;

      llvm::StringRef module_id = p_module->id;

      const bool module_exists = dap.modules.contains(module_id);
      if (remove_module && module_exists) {
        dap.modules.erase(module_id);
        dap.Send(protocol::Event{"module", protocol::ModuleEventBody{std::move(p_module).value(),
                                                                      protocol::ModuleEventBody::eReasonRemoved}});
      } else if (module_exists) {
        dap.Send(protocol::Event{"module", protocol::ModuleEventBody{std::move(p_module).value(),
                                                                       protocol::ModuleEventBody::eReasonChanged}});
      } else if (!remove_module) {
        dap.modules.insert(module_id);
        dap.Send(protocol::Event{"module", protocol::ModuleEventBody{std::move(p_module).value(),
                                                                       protocol::ModuleEventBody::eReasonNew}});
      }
    }
  } else if (event_mask & lldb::SBTarget::eBroadcastBitNewTargetCreated) {
    // For NewTargetCreated events, GetTargetFromEvent returns the parent
    // target, and GetCreatedTargetFromEvent returns the newly created
    // target. This is a multi-target handoff feature (a child target gets
    // its own DAP session) that doesn't apply to this project's
    // single-session model - left in place (harmless if never triggered)
    // rather than stripped, since it doesn't depend on DAPSessionManager.
    lldb::SBTarget created_target = lldb::SBTarget::GetCreatedTargetFromEvent(event);

    if (!target.IsValid() || !created_target.IsValid()) {
      DAP_LOG(dap.log, "Received NewTargetCreated event but parent or created target is invalid");
      return;
    }

    llvm::json::Object configuration;
    configuration.try_emplace("type", "lldb");
    configuration.try_emplace("name", created_target.GetTargetSessionName());

    llvm::json::Object session{{"targetId", created_target.GetGloballyUniqueID()},
                                {"debuggerId", created_target.GetDebugger().GetID()}};
    configuration.try_emplace("session", std::move(session));

    llvm::json::Object request;
    request.try_emplace("request", "attach");
    request.try_emplace("configuration", std::move(configuration));

    dap.SendReverseRequest<LogFailureResponseHandler>("startDebugging", std::move(request));
  }
}

static void HandleBreakpointEvent(const lldb::SBEvent &event, DebugService &dap) {
  const uint32_t event_mask = event.GetType();
  if (!(event_mask & lldb::SBTarget::eBroadcastBitBreakpointChanged))
    return;

  lldb::SBBreakpoint bp = lldb::SBBreakpoint::GetBreakpointFromEvent(event);
  if (!bp.IsValid())
    return;

  auto event_type = lldb::SBBreakpoint::GetBreakpointEventTypeFromEvent(event);
  auto breakpoint = Breakpoint(dap, bp);
  // If the breakpoint was set through DAP, it will have the
  // BreakpointBase::kDAPBreakpointLabel. Regardless of whether locations
  // were added, removed, or resolved, the breakpoint isn't going away and
  // the reason is always "changed".
  if ((event_type & lldb::eBreakpointEventTypeLocationsAdded || event_type & lldb::eBreakpointEventTypeLocationsRemoved ||
       event_type & lldb::eBreakpointEventTypeLocationsResolved) &&
      breakpoint.MatchesName(BreakpointBase::kDAPBreakpointLabel)) {
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

    dap.SendJSON(llvm::json::Value(std::move(bp_event)));
  }
}

static void HandleThreadEvent(const lldb::SBEvent &event, DebugService &dap) {
  uint32_t event_type = event.GetType();

  if (!(event_type & lldb::SBThread::eBroadcastBitStackChanged))
    return;

  lldb::SBThread thread = lldb::SBThread::GetThreadFromEvent(event);
  if (!thread.IsValid())
    return;

  SendInvalidatedEvent(dap, {protocol::InvalidatedEventBody::eAreaStacks}, thread.GetThreadID());
}

static void HandleDiagnosticEvent(const lldb::SBEvent &event, DebugService &dap) {
  lldb::SBStructuredData data = lldb::SBDebugger::GetDiagnosticFromEvent(event);
  if (!data.IsValid())
    return;

  std::string type = GetStringValue(data.GetValueForKey("type"));
  std::string message = GetStringValue(data.GetValueForKey("message"));
  dap.SendOutput(OutputType::Important, llvm::formatv("{0}: {1}", type, message).str());
}

// All events from the debugger, target, process, thread and frames for
// `dap`'s session are received in this function, which runs in its own
// thread (started from DebugService::StartEventThread). Unlike upstream's
// version, this loop is scoped to exactly one DebugService - no
// multi-session dispatch.
void EventThread(DebugService &dap) {
  std::string thread_name = llvm::formatv("trailer-dap.event_handler.{0}", dap.GetClientName());
  if (thread_name.length() > llvm::get_max_thread_name_length())
    thread_name = "trailer-dap.evt";
  llvm::set_thread_name(thread_name);

  lldb::SBListener listener = dap.debugger.GetListener();
  dap.broadcaster.AddListener(listener, eBroadcastBitStopEventThread);
  dap.debugger.GetBroadcaster().AddListener(listener, lldb::eBroadcastBitError | lldb::eBroadcastBitWarning);

  // Listen for thread events.
  listener.StartListeningForEventClass(dap.debugger, lldb::SBThread::GetBroadcasterClassName(),
                                        lldb::SBThread::eBroadcastBitStackChanged);

  lldb::SBEvent event;
  bool done = false;
  while (!done) {
    if (!listener.WaitForEvent(UINT32_MAX, event))
      continue;

    const uint32_t event_mask = event.GetType();
    if (lldb::SBProcess::EventIsProcessEvent(event)) {
      HandleProcessEvent(event, /*process_exited=*/done, dap);
    } else if (lldb::SBTarget::EventIsTargetEvent(event)) {
      HandleTargetEvent(event, dap);
    } else if (lldb::SBBreakpoint::EventIsBreakpointEvent(event)) {
      HandleBreakpointEvent(event, dap);
    } else if (lldb::SBThread::EventIsThreadEvent(event)) {
      HandleThreadEvent(event, dap);
    } else if (event_mask & lldb::eBroadcastBitError || event_mask & lldb::eBroadcastBitWarning) {
      HandleDiagnosticEvent(event, dap);
    } else if (event.BroadcasterMatchesRef(dap.broadcaster)) {
      if (event_mask & eBroadcastBitStopEventThread) {
        done = true;
      }
    }
  }
}

}  // namespace dap::debug_service
