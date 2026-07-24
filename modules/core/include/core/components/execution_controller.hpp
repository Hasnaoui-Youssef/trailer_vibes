//===-- execution_controller.hpp ------------------------------------------===//
//
// The execution component (see CLAUDE.md's DebugContext architecture):
// state machine over thread/process execution state, plus the live SB
// event-pump thread that drives it (`StartEventThread`/`EventThread` in the
// original fork). Carved out of DebugService - see PROJECT_STATUS.md.
//
// Everything here that used to call DebugService::Send/SendJSON now calls
// DebugContext::Send/SendJSON, which publish a WireMessageEvent instead of
// touching the Orchestrator directly (core never depends on dap_core) -
// see event_bus.hpp. dap_handlers' event_translator is the real subscriber
// that performs the equivalent of DebugService::Send's seq-assignment and
// wire write; the JSON each event carries is byte-identical to before
// (built here with the same dap::CreateEventObject/EmplaceSafeString
// helpers, now reachable from core because they moved to the LLDB-free,
// Transport-free dap_protocol target alongside the protocol value types).
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_EXECUTION_CONTROLLER_HPP_
#define TRAILER_CORE_COMPONENTS_EXECUTION_CONTROLLER_HPP_

#include <chrono>
#include <string>
#include <thread>

#include "dap/protocol/protocol_events.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBFormat.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBValue.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace core {

class DebugContext;

enum LaunchMethod { Launch, Attach, AttachForSuspendedLaunch };

enum ExecutionBroadcasterBits {
  eBroadcastBitStopEventThread = 1u << 0,
};

/// Creates a `protocol::Thread` for the given LLDB thread, formatted with
/// `format` if provided (falls back to a name/queue-derived label).
dap::protocol::Thread CreateThread(lldb::SBThread &thread, lldb::SBFormat &format);

/// Returns every thread in `process` as `protocol::Thread` (see CreateThread).
std::vector<dap::protocol::Thread> GetThreads(lldb::SBProcess process, lldb::SBFormat &format);

class ExecutionController {
public:
  explicit ExecutionController(DebugContext &context);
  ~ExecutionController();

  ExecutionController(const ExecutionController &) = delete;
  ExecutionController &operator=(const ExecutionController &) = delete;

  /// Broadcaster used purely to signal the event thread to stop (see
  /// StopEventHandlers) and as the breakpoint-event dispatch anchor - not a
  /// general-purpose event channel.
  lldb::SBBroadcaster broadcaster{"trailer-dap"};

  /// The focused thread for this session.
  lldb::tid_t focus_tid = LLDB_INVALID_THREAD_ID;

  /// Keep track of the last stop thread index IDs as threads won't go away
  /// unless we send a "thread" event to indicate the thread exited.
  llvm::DenseSet<lldb::tid_t> thread_ids;

  /// Map step-in target id to list of function targets that user can choose.
  llvm::DenseMap<lldb::addr_t, std::string> step_in_targets;

  bool stop_at_entry = false;
  /// Whether we have received the ConfigurationDone request.
  bool configuration_done = false;
  bool waiting_for_run_in_terminal = false;

  /// Starts (or re-starts) the SB event-pump thread listening on the
  /// debugger/target/broadcaster for process/target/breakpoint/thread
  /// events, translating them into DAP events.
  void StartEventThread();
  /// Signals the event thread to stop and joins it. Safe to call more than
  /// once - a no-op if the thread isn't running.
  void StopEventHandlers();

  /// Clears per-stop state before resuming (see DataManager::variables -
  /// expandable variable references are only valid while stopped).
  void WillContinue();

  lldb::SBError WaitForProcessToStop(std::chrono::seconds seconds);

  /// Sends target-based capabilities and custom capabilities once the
  /// target is known (see the `configurationDone` handler).
  void SendExtraCapabilities();
  void SendProcessEvent(LaunchMethod launch_method);
  /// Sends a `stopped` event for all threads as long as the process is
  /// stopped.
  llvm::Error SendThreadStoppedEvent(bool on_entry = false);
  void SendContinuedEvent();
  void SendProcessExitedEvent(lldb::SBProcess &process);
  void SendInvalidatedEvent(llvm::ArrayRef<dap::protocol::InvalidatedEventBody::Area> areas,
                            lldb::tid_t tid = LLDB_INVALID_THREAD_ID);
  void SendMemoryEvent(lldb::SBValue variable);
  void SendStdOutStdErr(lldb::SBProcess &process);

private:
  void SendThreadExitedEvent(lldb::tid_t tid);
  void HandleProcessEvent(const lldb::SBEvent &event, bool &process_exited);
  void HandleTargetEvent(const lldb::SBEvent &event);
  void HandleBreakpointEvent(const lldb::SBEvent &event);
  void HandleThreadEvent(const lldb::SBEvent &event);
  void HandleDiagnosticEvent(const lldb::SBEvent &event);
  void EventThreadMain();

  DebugContext &m_context;
  std::thread m_event_thread;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_EXECUTION_CONTROLLER_HPP_
