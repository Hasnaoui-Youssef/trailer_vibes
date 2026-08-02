#include "core/components/trace_manager.hpp"

#include <optional>
#include <utility>

#include "core/components/disassembly_manager.hpp"
#include "core/components/target_manager.hpp"
#include "core/debug_context.hpp"
#include "dap/dap_error.hpp"
#include "disassembler/program_disassembler.hpp"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBProcess.h"
#include "llvm/Support/Error.h"
#include "openocd_provider/openocd_provider.hpp"
#include "trace_model/instruction_trace_decode_config.hpp"

namespace core {

namespace {

llvm::Expected<trace_provider::TraceSession> BuildSession(DebugContext &context, const std::string &source) {
  std::expected<model::Etmv4Registers, std::string> registers = context.OpenOcd()->ReadETMv4Registers(source);
  if (!registers) return llvm::make_error<dap::DAPError>(registers.error());

  std::expected<std::string, std::string> core_name = context.OpenOcd()->GetCoreName();
  if (!core_name) return llvm::make_error<dap::DAPError>(core_name.error());

  llvm::Expected<const disasm::ProgramDisassembler &> program = context.Disassembly().Program();
  if (!program) return program.takeError();

  model::DeformatterConfig deformatter;
  deformatter.source_format = model::TraceSourceFormat::kFrameFormatted;
  deformatter.frame_sync = model::FrameSyncMode::kMemAligned;
  deformatter.reset_on_4x_fsync = true;

  model::InstructionTraceDecodeConfig::Builder builder;
  builder.SetProgramPath(context.Session().configuration.program)
      .SetCoreName(*core_name)
      .SetDeformatter(deformatter)
      .SetRegisters(*registers);
  model::InstructionTraceDecodeConfig config = std::move(builder).Build();

  const disasm::ProgramDisassembler *disassembler = &*program;
  trace_provider::ResolveInstruction resolve = [disassembler](uint64_t address) {
    return disassembler->InstructionInfoAt(address);
  };

  return trace_provider::TraceSession(std::move(config), disassembler->load_segments(), std::move(resolve));
}

}  // namespace

TraceManager::TraceManager(DebugContext &context, std::string sink, std::string source,
                            trace_provider::TraceSession session)
    : m_context(context), sink_(std::move(sink)), source_(std::move(source)), session_(std::move(session)) {
  decode_worker_ = std::thread(&TraceManager::DecodeWorkerMain, this);
}

TraceManager::~TraceManager() {
  // Unsubscribe first so no further OnCapture() calls can enqueue work -
  // OpenOcdProvider::UnsubscribeTrace runs on server_loop and only returns
  // once that thread has cycled past any capture callback already in
  // flight, so no OnCapture() call is running concurrently with this either.
  if (providers::OpenOcdProvider *openocd = m_context.OpenOcd()) (void)openocd->UnsubscribeTrace(sink_);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
  }
  decode_cv_.notify_one();
  if (decode_worker_.joinable())
    decode_worker_.join();
}

std::expected<std::unique_ptr<TraceManager>, std::string> TraceManager::Create(DebugContext &context) {
  if (!context.OpenOcd()) return std::unexpected("trace requires an OpenOCD-backed launch session");

  std::expected<std::vector<providers::TmcObject>, std::string> sinks = context.OpenOcd()->ListTraceSinks();
  if (!sinks) return std::unexpected(sinks.error());
  if (sinks->empty()) return std::unexpected("no trace sink found - check the sourced OpenOCD config");

  std::expected<std::vector<providers::Etmv4Object>, std::string> sources = context.OpenOcd()->ListTraceSources();
  if (!sources) return std::unexpected(sources.error());
  if (sources->empty()) return std::unexpected("no trace source found - check the sourced OpenOCD config");

  const std::string sink = sinks->front().name;
  const std::string source = sources->front().name;

  llvm::Expected<trace_provider::TraceSession> session = BuildSession(context, source);
  if (!session) return std::unexpected(llvm::toString(session.takeError()));

  auto manager = std::unique_ptr<TraceManager>(new TraceManager(context, sink, source, std::move(*session)));

  TraceManager *raw = manager.get();
  std::expected<void, std::string> subscribed = context.OpenOcd()->SubscribeTrace(
      sink, [raw](std::span<const std::byte> data, bool is_barrier) { raw->OnCapture(data, is_barrier); });
  if (!subscribed) return std::unexpected(subscribed.error());

  return manager;
}

llvm::Error TraceManager::Enable() {
  llvm::Expected<trace_provider::TraceSession> session = BuildSession(m_context, source_);
  if (!session) return session.takeError();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    session_ = std::move(*session);
    pending_.clear();
    decode_queue_.clear();
    instructions_.clear();
    function_blocks_.clear();
    gaps_.clear();
  }

  if (std::expected<void, std::string> ok = m_context.OpenOcd()->EnableTrace(sink_); !ok)
    return llvm::make_error<dap::DAPError>(ok.error());
  if (std::expected<void, std::string> ok = m_context.OpenOcd()->EnableTrace(source_); !ok)
    return llvm::make_error<dap::DAPError>(ok.error());

  {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = true;
  }
  m_context.Emit(TraceStatusEvent{Status()});
  return llvm::Error::success();
}

llvm::Error TraceManager::Disable() {
  if (std::expected<void, std::string> ok = m_context.OpenOcd()->DisableTrace(source_); !ok)
    return llvm::make_error<dap::DAPError>(ok.error());
  if (std::expected<void, std::string> ok = m_context.OpenOcd()->DisableTrace(sink_); !ok)
    return llvm::make_error<dap::DAPError>(ok.error());

  {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = false;
    pending_.clear();
    decode_queue_.clear();
  }
  m_context.Emit(TraceStatusEvent{Status()});
  return llvm::Error::success();
}

bool TraceManager::enabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return enabled_;
}

dap::protocol::TraceStatusResponseBody TraceManager::Status() const {
  dap::protocol::TraceStatusResponseBody body;
  body.available = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    body.enabled = enabled_;
    body.instructionCount = instructions_.size();
    body.functionBlockCount = function_blocks_.size();
  }
  if (!body.enabled) {
    body.reason = dap::protocol::TraceStatusReason::eTraceStatusReasonIdle;
    return body;
  }
  const bool stopped = m_context.WithTarget([&]() {
    return lldb::SBDebugger::StateIsStoppedState(m_context.Target().GetProcess().GetState());
  });
  body.reason = stopped ? dap::protocol::TraceStatusReason::eTraceStatusReasonArmedAwaitingResume
                        : dap::protocol::TraceStatusReason::eTraceStatusReasonCapturing;
  return body;
}

void TraceManager::OnCapture(std::span<const std::byte> data, bool is_barrier) {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.insert(pending_.end(), data.begin(), data.end());
  if (is_barrier) return;

  if (!enabled_) {
    pending_.clear();
    return;
  }

  decode_queue_.push_back(std::move(pending_));
  pending_.clear();
  decode_cv_.notify_one();
}

void TraceManager::DecodeWorkerMain() {
  for (;;) {
    std::vector<std::byte> chunk;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      decode_cv_.wait(lock, [this] { return !decode_queue_.empty() || shutting_down_; });
      if (shutting_down_) return;
      chunk = std::move(decode_queue_.front());
      decode_queue_.pop_front();
    }

    std::optional<TraceDataEvent> data_event;
    std::optional<dap::protocol::TraceStatusResponseBody> status_event;
    std::optional<std::string> decode_error;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!enabled_) continue;

      std::expected<trace_provider::DecodedIncrement, std::string> increment = session_.Append(chunk);
      if (!increment) {
        decode_error = increment.error();
        dap::protocol::TraceStatusResponseBody status;
        status.available = true;
        status.enabled = enabled_;
        status.reason = dap::protocol::TraceStatusReason::eTraceStatusReasonDecodeError;
        status.detail = increment.error();
        status.instructionCount = instructions_.size();
        status.functionBlockCount = function_blocks_.size();
        status_event = std::move(status);
      } else {
        TraceDataEvent event;
        event.first_instruction_index = instructions_.size();
        event.first_function_block_index = function_blocks_.size();

        instructions_.insert(instructions_.end(), increment->instructions.begin(), increment->instructions.end());
        function_blocks_.insert(function_blocks_.end(), increment->function_blocks.begin(),
                                 increment->function_blocks.end());
        gaps_.insert(gaps_.end(), increment->gaps.begin(), increment->gaps.end());

        dap::protocol::TraceStatusResponseBody status;
        status.available = true;
        status.enabled = enabled_;
        status.reason = dap::protocol::TraceStatusReason::eTraceStatusReasonCapturing;
        status.instructionCount = instructions_.size();
        status.functionBlockCount = function_blocks_.size();

        event.instructions = std::move(increment->instructions);
        event.function_blocks = std::move(increment->function_blocks);
        event.gaps = std::move(increment->gaps);
        data_event = std::move(event);
        status_event = std::move(status);
      }
    }

    if (decode_error) m_context.SendOutput(OutputCategory::Important, "trace decode failed: " + *decode_error);
    if (data_event) m_context.Emit(std::move(*data_event));
    if (status_event) m_context.Emit(TraceStatusEvent{std::move(*status_event)});
  }
}

}  // namespace core
