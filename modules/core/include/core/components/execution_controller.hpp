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
#include "openocd_provider/openocd_provider.hpp"
#include <atomic>
#include <vector>

namespace core {

class DebugContext;

enum LaunchMethod { Launch, Attach, AttachForSuspendedLaunch };

enum ExecutionBroadcasterBits {
  eBroadcastBitStopEventThread = 1u << 0,
};

dap::protocol::Thread CreateThread(lldb::SBThread &thread, lldb::SBFormat &format);

std::vector<dap::protocol::Thread> GetThreads(lldb::SBProcess process, lldb::SBFormat &format);

class ExecutionController {
public:
  explicit ExecutionController(DebugContext &context);
  ~ExecutionController();

  ExecutionController(const ExecutionController &) = delete;
  ExecutionController &operator=(const ExecutionController &) = delete;

  lldb::SBBroadcaster broadcaster{"trailer-dap"};

  lldb::tid_t focus_tid = LLDB_INVALID_THREAD_ID;

  llvm::DenseSet<lldb::tid_t> thread_ids;

  llvm::DenseMap<lldb::addr_t, std::string> step_in_targets;

  bool stop_at_entry = false;
  bool configuration_done = false;
  bool waiting_for_run_in_terminal = false;

  std::atomic<bool> reset_pending_stop{false};
  std::atomic<bool> reset_resume_after_stop{false};

  std::vector<dap::protocol::Thread> initial_thread_list;

  void StartEventThread();
  void StopEventHandlers();

  void WillContinue();

  lldb::SBError WaitForProcessToStop(std::chrono::seconds seconds);

  llvm::Error ConfigurationDone();

  llvm::Expected<dap::protocol::ContinueResponseBody> Continue(const dap::protocol::ContinueArguments &args);
  llvm::Error Pause();
  llvm::Error Next(const dap::protocol::NextArguments &args);
  llvm::Error StepIn(const dap::protocol::StepInArguments &args);
  llvm::Error StepOut(const dap::protocol::StepOutArguments &args);
  llvm::Expected<dap::protocol::StepInTargetsResponseBody> GetStepInTargets(
      const dap::protocol::StepInTargetsArguments &args);

  llvm::Expected<dap::protocol::ThreadsResponseBody> GetThreadsRequest();

  llvm::Expected<dap::protocol::ExceptionInfoResponseBody> GetExceptionInfoRequest(
      const dap::protocol::ExceptionInfoArguments &args);

  void SendExtraCapabilities();
  void SendProcessEvent(LaunchMethod launch_method);
  llvm::Error SendThreadStoppedEvent(bool on_entry = false);
  void SendContinuedEvent();
  void SendProcessExitedEvent(lldb::SBProcess &process);
  void SendInvalidatedEvent(llvm::ArrayRef<dap::protocol::InvalidatedEventBody::Area> areas,
                            lldb::tid_t tid = LLDB_INVALID_THREAD_ID);
  void SendMemoryEvent(lldb::SBValue variable);
  void SendStdOutStdErr(lldb::SBProcess &process);

  // Runs on OpenOCD's server thread - must not touch the LLDB SB API.
  void OnOpenOcdTargetState(const providers::OpenOcdProvider::TargetStateChange &change);

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
