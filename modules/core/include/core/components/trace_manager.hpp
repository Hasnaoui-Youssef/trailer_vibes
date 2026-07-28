#ifndef TRAILER_CORE_COMPONENTS_TRACE_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_TRACE_MANAGER_HPP_

#include <cstddef>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "llvm/Support/Error.h"
#include "trace_model/function_block.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_model/trace_gap.hpp"
#include "trace_provider/trace_session.hpp"

namespace core {

class DebugContext;

// Trace is armed by an explicit command only (never from launch config).
// The first TMC sink and first ETMv4 source found in the sourced OpenOCD
// config are used - the client never names components (see CLAUDE.md).
class TraceManager {
public:
  static std::expected<std::unique_ptr<TraceManager>, std::string> Create(DebugContext &context);

  TraceManager(const TraceManager &) = delete;
  TraceManager &operator=(const TraceManager &) = delete;
  ~TraceManager();

  llvm::Error Enable();
  llvm::Error Disable();
  bool enabled() const;

private:
  TraceManager(DebugContext &context, std::string sink, std::string source,
               trace_provider::TraceSession session);

  void OnCapture(std::span<const std::byte> data, bool is_barrier);

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
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_TRACE_MANAGER_HPP_
