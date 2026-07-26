//===-- execution_controller.hpp ------------------------------------------===//
//
// State machine over thread/process execution, plus the live SB event-pump
// thread that drives it. Events go out via DebugContext::Emit (EventBus),
// never straight to the Orchestrator.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_COMPONENTS_EXECUTION_CONTROLLER_HPP_
#define TRAILER_CORE_COMPONENTS_EXECUTION_CONTROLLER_HPP_

#include <chrono>
#include <string>
#include <thread>

#include "dap/protocol/protocol_events.hpp"
#include "dap/protocol/protocol_requests.hpp"
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

  /// The initial thread list upon attaching, cached by
  /// ConfigurationDoneRequestHandler and consumed (once) by the first
  /// `threads` request - see ThreadsRequestHandler::Run.
  std::vector<dap::protocol::Thread> initial_thread_list;

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

  // Validates the process is stopped as expected, sends target-based/extra
  // capabilities, caches the initial thread list, sends the "process"
  // event, and either reports the entry stop or resumes - everything the
  // `configurationDone` request needs short of the handler's own
  // PrintIntroductionMessage(). Locks the API mutex internally for its full
  // duration.
  llvm::Error ConfigurationDone();

  // The following back the execution-control requests (continue, pause,
  // next, stepIn, stepInTargets, stepOut) one-to-one. Each locks the API
  // mutex internally for its full duration.
  llvm::Expected<dap::protocol::ContinueResponseBody> Continue(const dap::protocol::ContinueArguments &args);
  llvm::Error Pause();
  llvm::Error Next(const dap::protocol::NextArguments &args);
  llvm::Error StepIn(const dap::protocol::StepInArguments &args);
  llvm::Error StepOut(const dap::protocol::StepOutArguments &args);
  llvm::Expected<dap::protocol::StepInTargetsResponseBody> GetStepInTargets(
      const dap::protocol::StepInTargetsArguments &args);

  // Consumes the cached initial_thread_list once (set by ConfigurationDone),
  // otherwise requires the process to be stopped and lists its threads.
  llvm::Expected<dap::protocol::ThreadsResponseBody> GetThreadsRequest();

  llvm::Expected<dap::protocol::ExceptionInfoResponseBody> GetExceptionInfoRequest(
      const dap::protocol::ExceptionInfoArguments &args);

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
