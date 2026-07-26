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
#include "core/components/target_manager.hpp"
#include "core/debug_context.hpp"
#include "core/lldb_utils.hpp"
#include "dap/dap_error.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBInstruction.h"
#include "lldb/API/SBInstructionList.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBPlatform.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBValue.h"
#include "lldb/API/SBSymbolContext.h"
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

llvm::Error ExecutionController::ConfigurationDone() {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  // Ensure any command scripts did not leave us in an unexpected state.
  lldb::SBProcess process = m_context.Target().GetProcess();
  if (!process.IsValid() || !lldb::SBDebugger::StateIsStoppedState(process.GetState()))
    return llvm::make_error<DAPError>(
        "Expected process to be stopped.\r\n\r\nProcess is in an unexpected "
        "state and may have missed an initial configuration. Please check that "
        "any debugger command scripts are not resuming the process during the "
        "launch sequence.");

  // Waiting until 'configurationDone' to send target based capabilities in case
  // the launch or attach scripts adjust the target. The initial dummy target
  // may have different capabilities than the final target.
  //
  // Also send here custom capabilities to the client, which is consumed by the
  // lldb-dap specific editor extension.
  SendExtraCapabilities();

  // Clients can request a baseline of currently existing threads after we
  // acknowledge the configurationDone request. Client requests the baseline
  // of currently existing threads after a successful launch or attach by
  // sending a 'threads' request right after receiving the configurationDone
  // response. Obtain the list of threads before we resume the process.
  initial_thread_list = GetThreads(process, m_context.Data().thread_format);

  SendProcessEvent(m_context.Session().is_attach ? Attach : Launch);

  if (stop_at_entry)
    return SendThreadStoppedEvent(/*on_entry=*/true);

  return ToError(process.Continue());
}

llvm::Expected<protocol::ContinueResponseBody> ExecutionController::Continue(const protocol::ContinueArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  lldb::SBProcess process = m_context.Target().GetProcess();
  lldb::SBError error;

  if (!lldb::SBDebugger::StateIsStoppedState(process.GetState()))
    return llvm::make_error<NotStoppedError>();

  if (args.singleThread)
    m_context.GetLLDBThread(args.threadId).Resume(error);
  else
    error = process.Continue();

  if (error.Fail())
    return ToError(error);

  protocol::ContinueResponseBody body;
  body.allThreadsContinued = !args.singleThread;
  return body;
}

llvm::Error ExecutionController::Pause() {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  lldb::SBProcess process = m_context.Target().GetProcess();
  lldb::SBError error = process.Stop();
  return ToError(error);
}

llvm::Error ExecutionController::Next(const protocol::NextArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  lldb::SBThread thread = m_context.GetLLDBThread(args.threadId);
  if (!thread.IsValid())
    return llvm::make_error<DAPError>("invalid thread");

  if (!lldb::SBDebugger::StateIsStoppedState(m_context.Target().GetProcess().GetState()))
    return llvm::make_error<NotStoppedError>();

  // Remember the thread ID that caused the resume so we can set the
  // "threadCausedFocus" boolean value in the "stopped" events.
  focus_tid = thread.GetThreadID();
  lldb::SBError error;
  if (args.granularity == protocol::eSteppingGranularityInstruction) {
    thread.StepInstruction(/*step_over=*/true, error);
  } else {
    thread.StepOver(args.singleThread ? lldb::eOnlyThisThread : lldb::eOnlyDuringStepping, error);
  }

  return ToError(error);
}

llvm::Error ExecutionController::StepIn(const protocol::StepInArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  lldb::SBThread thread = m_context.GetLLDBThread(args.threadId);
  if (!thread.IsValid())
    return llvm::make_error<DAPError>("invalid thread");

  // Remember the thread ID that caused the resume so we can set the
  // "threadCausedFocus" boolean value in the "stopped" events.
  focus_tid = thread.GetThreadID();

  if (!lldb::SBDebugger::StateIsStoppedState(m_context.Target().GetProcess().GetState()))
    return llvm::make_error<NotStoppedError>();

  lldb::SBError error;
  if (args.granularity == protocol::eSteppingGranularityInstruction) {
    thread.StepInstruction(/*step_over=*/false, error);
    return ToError(error);
  }

  std::string step_in_target;
  auto it = step_in_targets.find(args.targetId.value_or(0));
  if (it != step_in_targets.end())
    step_in_target = it->second;

  lldb::RunMode run_mode = args.singleThread ? lldb::eOnlyThisThread : lldb::eOnlyDuringStepping;
  thread.StepInto(step_in_target.c_str(), LLDB_INVALID_LINE_NUMBER, error, run_mode);
  return ToError(error);
}

llvm::Error ExecutionController::StepOut(const protocol::StepOutArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  lldb::SBThread thread = m_context.GetLLDBThread(args.threadId);
  if (!thread.IsValid())
    return llvm::make_error<DAPError>("invalid thread");

  if (!lldb::SBDebugger::StateIsStoppedState(m_context.Target().GetProcess().GetState()))
    return llvm::make_error<NotStoppedError>();

  // Remember the thread ID that caused the resume so we can set the
  // "threadCausedFocus" boolean value in the "stopped" events.
  focus_tid = thread.GetThreadID();
  lldb::SBError error;
  thread.StepOut(error);

  return ToError(error);
}

llvm::Expected<protocol::StepInTargetsResponseBody> ExecutionController::GetStepInTargets(
    const protocol::StepInTargetsArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  step_in_targets.clear();
  const lldb::SBFrame frame = m_context.GetLLDBFrame(args.frameId);
  if (!frame.IsValid())
    return llvm::make_error<DAPError>("Failed to get frame for input frameId.");

  lldb::SBAddress pc_addr = frame.GetPCAddress();
  lldb::SBAddress line_end_addr = pc_addr.GetLineEntry().GetSameLineContiguousAddressRangeEnd(true);
  lldb::SBInstructionList insts = m_context.Target().ReadInstructions(pc_addr, line_end_addr, /*flavor_string=*/nullptr);

  if (!insts.IsValid())
    return llvm::make_error<DAPError>("Failed to get instructions for frame.");

  protocol::StepInTargetsResponseBody body;
  const size_t num_insts = insts.GetSize();
  for (size_t i = 0; i < num_insts; ++i) {
    lldb::SBInstruction inst = insts.GetInstructionAtIndex(i);
    if (!inst.IsValid())
      break;

    const lldb::addr_t inst_addr = inst.GetAddress().GetLoadAddress(m_context.Target());
    if (inst_addr == LLDB_INVALID_ADDRESS)
      break;

    // Note: currently only x86/x64 supports flow kind.
    const lldb::InstructionControlFlowKind flow_kind = inst.GetControlFlowKind(m_context.Target());

    if (flow_kind == lldb::eInstructionControlFlowKindCall) {
      const llvm::StringRef call_operand_name = inst.GetOperands(m_context.Target());
      lldb::addr_t call_target_addr = LLDB_INVALID_ADDRESS;
      if (call_operand_name.getAsInteger(0, call_target_addr))
        continue;

      const lldb::SBAddress call_target_load_addr = m_context.Target().ResolveLoadAddress(call_target_addr);
      if (!call_target_load_addr.IsValid())
        continue;

      // The existing ThreadPlanStepInRange only accept step in target
      // function with debug info.
      lldb::SBSymbolContext sc =
          m_context.Target().ResolveSymbolContextForAddress(call_target_load_addr, lldb::eSymbolContextFunction);

      llvm::StringRef step_in_target_name;
      if (sc.IsValid() && sc.GetFunction().IsValid())
        step_in_target_name = sc.GetFunction().GetDisplayName();

      // Skip call sites if we fail to resolve its symbol name.
      if (step_in_target_name.empty())
        continue;

      protocol::StepInTarget target;
      target.id = inst_addr;
      target.label = step_in_target_name;
      step_in_targets.try_emplace(inst_addr, step_in_target_name);
      body.targets.emplace_back(std::move(target));
    }
  }
  return body;
}

llvm::Expected<protocol::ThreadsResponseBody> ExecutionController::GetThreadsRequest() {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  lldb::SBProcess process = m_context.Target().GetProcess();
  std::vector<protocol::Thread> threads;

  if (!initial_thread_list.empty()) {
    threads = initial_thread_list;
    initial_thread_list.clear();
  } else {
    if (!lldb::SBDebugger::StateIsStoppedState(process.GetState()))
      return llvm::make_error<NotStoppedError>();
    threads = GetThreads(process, m_context.Data().thread_format);
  }

  if (threads.empty())
    return llvm::make_error<DAPError>("failed to retrieve threads from process");

  return protocol::ThreadsResponseBody{threads};
}

llvm::Expected<protocol::ExceptionInfoResponseBody>
ExecutionController::GetExceptionInfoRequest(const protocol::ExceptionInfoArguments &args) {
  lldb::SBMutex lock = m_context.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  lldb::SBThread thread = m_context.GetLLDBThread(args.threadId);
  if (!thread.IsValid())
    return llvm::make_error<DAPError>(llvm::formatv("Invalid thread id: {}", args.threadId).str());

  protocol::ExceptionInfoResponseBody response;
  response.breakMode = protocol::eExceptionBreakModeAlways;
  switch (thread.GetStopReason()) {
  case lldb::eStopReasonSignal:
    response.exceptionId = "signal";
    break;
  case lldb::eStopReasonBreakpoint: {
    const ExceptionBreakpoint *exc_bp = m_context.Breakpoints().GetExceptionBPFromStopReason(thread);
    if (exc_bp) {
      response.exceptionId = exc_bp->GetFilter();
      response.description = exc_bp->GetLabel();
    } else {
      response.exceptionId = "exception";
    }
  } break;
  default:
    response.exceptionId = "exception";
  }

  lldb::SBStream stream;
  if (response.description.empty() && thread.GetStopDescription(stream))
    response.description = {stream.GetData(), stream.GetSize()};

  if (lldb::SBValue exception = thread.GetCurrentException()) {
    stream.Clear();
    response.details = protocol::ExceptionDetails{};
    if (exception.GetDescription(stream))
      response.details->message = {stream.GetData(), stream.GetSize()};

    if (lldb::SBThread exception_backtrace = thread.GetCurrentExceptionBacktrace()) {
      stream.Clear();
      exception_backtrace.GetDescription(stream);
      for (uint32_t idx = 0; idx < exception_backtrace.GetNumFrames(); idx++) {
        lldb::SBFrame frame = exception_backtrace.GetFrameAtIndex(idx);
        frame.GetDescription(stream);
      }
      response.details->stackTrace = {stream.GetData(), stream.GetSize()};
    }
  }
  return response;
}

void ExecutionController::SendExtraCapabilities() {
  protocol::Capabilities capabilities = m_context.GetCustomCapabilities();
  llvm::DenseSet<protocol::AdapterFeature> target_capabilities = GetTargetBasedCapabilities(m_context);

  capabilities.supportedFeatures.insert(target_capabilities.begin(), target_capabilities.end());

  protocol::CapabilitiesEventBody body;
  body.capabilities = std::move(capabilities);

  // Only notify the client if supportedFeatures changed.
  if (!body.capabilities.supportedFeatures.empty())
    m_context.Emit(CapabilitiesEvent{std::move(body)});
}

void ExecutionController::SendProcessEvent(LaunchMethod launch_method) {
  lldb::SBFileSpec exe_fspec = m_context.Target().GetExecutable();
  char exe_path[4096];
  exe_fspec.GetPath(exe_path, sizeof(exe_path));

  protocol::ProcessEventBody body;
  body.name = exe_path;
  body.systemProcessId = m_context.Target().GetProcess().GetProcessID();
  body.isLocalProcess = m_context.Target().GetPlatform().IsHost();
  body.pointerSize = m_context.Target().GetAddressByteSize() * 8;
  switch (launch_method) {
  case Launch:
    body.startMethod = "launch";
    break;
  case Attach:
    body.startMethod = "attach";
    break;
  case AttachForSuspendedLaunch:
    body.startMethod = "attachForSuspendedLaunch";
    break;
  }
  m_context.Emit(ProcessEvent{std::move(body)});
}

void ExecutionController::SendThreadExitedEvent(lldb::tid_t tid) {
  m_context.Emit(ThreadExitedEvent{protocol::ThreadExitedEventBody{tid}});
}

// Create a "StoppedEvent" body for a LLDB thread object. Mirrors
// debug_service::CreateThreadStopped exactly - see json_utils.cc.
static protocol::StoppedEventBody CreateThreadStoppedEvent(DebugContext &context, lldb::SBThread &thread,
                                                            uint32_t stop_id) {
  protocol::StoppedEventBody body;
  switch (thread.GetStopReason()) {
  case lldb::eStopReasonTrace:
  case lldb::eStopReasonPlanComplete:
    body.reason = "step";
    break;
  case lldb::eStopReasonBreakpoint: {
    ExceptionBreakpoint *exc_bp = context.Breakpoints().GetExceptionBPFromStopReason(thread);
    if (exc_bp) {
      body.reason = "exception";
      body.description = exc_bp->GetLabel();
    } else {
      body.reason = "breakpoint";
      std::vector<int64_t> bp_ids;
      std::ostringstream desc_sstream;
      desc_sstream << "breakpoint";
      for (size_t idx = 0; idx < thread.GetStopReasonDataCount(); idx += 2) {
        lldb::break_id_t bp_id = thread.GetStopReasonDataAtIndex(idx);
        lldb::break_id_t bp_loc_id = thread.GetStopReasonDataAtIndex(idx + 1);
        bp_ids.push_back(bp_id);
        desc_sstream << " " << bp_id << "." << bp_loc_id;
      }
      body.hitBreakpointIds = std::move(bp_ids);
      body.description = desc_sstream.str();
    }
  } break;
  case lldb::eStopReasonWatchpoint: {
    body.reason = "data breakpoint";
    lldb::break_id_t bp_id = thread.GetStopReasonDataAtIndex(0);
    body.hitBreakpointIds = std::vector<int64_t>{bp_id};
    body.description = llvm::formatv("data breakpoint {0}", bp_id).str();
  } break;
  case lldb::eStopReasonInstrumentation:
    body.reason = "breakpoint";
    break;
  case lldb::eStopReasonProcessorTrace:
    body.reason = "processor trace";
    break;
  case lldb::eStopReasonHistoryBoundary:
    body.reason = "history boundary";
    break;
  case lldb::eStopReasonSignal:
  case lldb::eStopReasonException:
    body.reason = "exception";
    break;
  case lldb::eStopReasonExec:
    body.reason = "entry";
    break;
  case lldb::eStopReasonFork:
    body.reason = "fork";
    break;
  case lldb::eStopReasonVFork:
    body.reason = "vfork";
    break;
  case lldb::eStopReasonVForkDone:
    body.reason = "vforkdone";
    break;
  case lldb::eStopReasonInterrupt:
    body.reason = "async interrupt";
    break;
  case lldb::eStopReasonThreadExiting:
  case lldb::eStopReasonInvalid:
  case lldb::eStopReasonNone:
    break;
  }
  if (stop_id == 0)
    body.reason = "entry";
  const lldb::tid_t tid = thread.GetThreadID();
  body.threadId = tid;
  // If no description has been set, then set it to the default thread
  // stopped description.
  if (!body.description) {
    char description[1024];
    if (thread.GetStopDescription(description, sizeof(description)))
      body.description = std::string(description);
  }
  // "threadCausedFocus" is used in tests to validate breaking behavior.
  if (tid == context.Execution().focus_tid)
    body.threadCausedFocus = true;
  body.preserveFocusHint = tid != context.Execution().focus_tid;
  body.allThreadsStopped = true;
  return body;
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
    m_context.Emit(StoppedEvent{CreateThreadStoppedEvent(m_context, thread, stop_id)});
  } else {
    for (uint32_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
      lldb::SBThread thread = process.GetThreadAtIndex(thread_idx);
      thread_ids.insert(thread.GetThreadID());
      if (ThreadHasStopReason(thread))
        m_context.Emit(StoppedEvent{CreateThreadStoppedEvent(m_context, thread, stop_id)});
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

  protocol::ContinuedEventBody body;
  body.threadId = focus_tid;
  body.allThreadsContinued = true;
  m_context.Emit(ContinuedEvent{std::move(body)});
}

void ExecutionController::SendProcessExitedEvent(lldb::SBProcess &process) {
  protocol::ExitedEventBody body;
  body.exitCode = process.GetExitStatus();
  m_context.Emit(ExitedEvent{std::move(body)});
}

void ExecutionController::SendInvalidatedEvent(llvm::ArrayRef<protocol::InvalidatedEventBody::Area> areas,
                                               lldb::tid_t tid) {
  if (!m_context.ClientFeatureEnabled(protocol::eClientFeatureInvalidatedEvent))
    return;
  protocol::InvalidatedEventBody body;
  body.areas = areas;

  if (tid != LLDB_INVALID_THREAD_ID)
    body.threadId = tid;

  m_context.Emit(InvalidatedEvent{std::move(body)});
}

void ExecutionController::SendMemoryEvent(lldb::SBValue variable) {
  if (!m_context.ClientFeatureEnabled(protocol::eClientFeatureMemoryEvent))
    return;
  protocol::MemoryEventBody body;
  body.memoryReference = variable.GetLoadAddress();
  body.count = variable.GetByteSize();
  if (body.memoryReference == LLDB_INVALID_ADDRESS)
    return;
  m_context.Emit(MemoryEvent{std::move(body)});
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
        m_context.Emit(ModuleEvent{
            protocol::ModuleEventBody{std::move(p_module).value(), protocol::ModuleEventBody::eReasonRemoved}});
      } else if (module_exists) {
        m_context.Emit(ModuleEvent{
            protocol::ModuleEventBody{std::move(p_module).value(), protocol::ModuleEventBody::eReasonChanged}});
      } else if (!remove_module) {
        modules.modules.insert(module_id);
        m_context.Emit(ModuleEvent{
            protocol::ModuleEventBody{std::move(p_module).value(), protocol::ModuleEventBody::eReasonNew}});
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

    m_context.Emit(BreakpointEvent{protocol::BreakpointEventBody{std::move(protocol_bp)}});
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
