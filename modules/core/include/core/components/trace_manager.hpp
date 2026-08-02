#ifndef TRAILER_CORE_COMPONENTS_TRACE_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_TRACE_MANAGER_HPP_

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "dap/protocol/protocol_requests.hpp"
#include "llvm/Support/Error.h"
#include "trace_model/function_block.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_model/trace_gap.hpp"
#include "trace_provider/trace_session.hpp"

namespace core {

class DebugContext;

// Trace is armed by an explicit command only (never from launch config).
// The first TMC sink and first ETMv4 source found in the sourced OpenOCD
// config are used
class TraceManager {
public:
  static std::expected<std::unique_ptr<TraceManager>, std::string> Create(DebugContext &context);

  TraceManager(const TraceManager &) = delete;
  TraceManager &operator=(const TraceManager &) = delete;
  ~TraceManager();

  llvm::Error Enable();
  llvm::Error Disable();
  bool enabled() const;
  dap::protocol::TraceStatusResponseBody Status() const;

private:
  TraceManager(DebugContext &context, std::string sink, std::string source,
               trace_provider::TraceSession session);

  // Fast path, runs on OpenOCD's server_loop thread: accumulates raw bytes
  // and, once a decodable unit is ready, hands it to decode_worker_ via
  // decode_queue_ instead of decoding inline.
  void OnCapture(std::span<const std::byte> data, bool is_barrier);

  // The only thread that ever calls session_.Append() - trace increments
  // are order-dependent, so decode is single-consumer, not a pool.
  void DecodeWorkerMain();

  DebugContext &m_context;
  std::string sink_;
  std::string source_;

  mutable std::mutex mutex_;
  trace_provider::TraceSession session_;
  bool enabled_ = false;
  std::vector<std::byte> pending_;
  std::vector<model::ReconstructedInstruction> instructions_;
  std::vector<model::FunctionBlock> function_blocks_;
  std::vector<model::TraceGap> gaps_;

  std::thread decode_worker_;
  std::condition_variable decode_cv_;
  std::deque<std::vector<std::byte>> decode_queue_;
  bool shutting_down_ = false;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_TRACE_MANAGER_HPP_
