#ifndef TRAILER_CORE_EVENT_BUS_HPP_
#define TRAILER_CORE_EVENT_BUS_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

#include "dap/protocol/protocol_events.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "trace_model/function_block.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_model/trace_gap.hpp"

namespace core {

enum class OutputCategory { Console, Important, Stdout, Stderr, Telemetry };

struct OutputEvent {
  OutputCategory category;
  std::string text;
};

struct StoppedEvent {
  dap::protocol::StoppedEventBody body;
};

struct ContinuedEvent {
  dap::protocol::ContinuedEventBody body;
};

struct ExitedEvent {
  dap::protocol::ExitedEventBody body;
};

struct ThreadExitedEvent {
  dap::protocol::ThreadExitedEventBody body;
};

struct ProcessEvent {
  dap::protocol::ProcessEventBody body;
};

struct CapabilitiesEvent {
  dap::protocol::CapabilitiesEventBody body;
};

struct InvalidatedEvent {
  dap::protocol::InvalidatedEventBody body;
};

struct MemoryEvent {
  dap::protocol::MemoryEventBody body;
};

struct ModuleEvent {
  dap::protocol::ModuleEventBody body;
};

struct BreakpointEvent {
  dap::protocol::BreakpointEventBody body;
};

// The "terminated" event's body is `{"$__lldb_statistics": <dump>}.
struct TerminatedEvent {
  std::string statistics_json;
};

// Carries model:: types rather than a protocol::* body - the DAP layer
// resolves source frames and converts to wire JSON at serialization time.
struct TraceDataEvent {
  uint64_t first_instruction_index;
  uint64_t first_function_block_index;
  std::vector<model::ReconstructedInstruction> instructions;
  std::vector<model::FunctionBlock> function_blocks;
  std::vector<model::TraceGap> gaps;
};

enum class ResetPhase { Started, Complete };

struct ResetEvent {
  ResetPhase phase;
};

struct TraceStatusEvent {
  dap::protocol::TraceStatusResponseBody body;
};

// A live memory watch's periodic read. Raw bytes, not a protocol::* body -
// there's no request/response shape to share, unlike TraceStatusEvent.
struct WatchDataEvent {
  int64_t watch_id;
  uint64_t address;
  std::vector<std::byte> data;
  uint64_t sequence;
};

// Fires only on active/error transitions, not on every poll - see
// WatchManager::WorkerMain.
struct WatchStateEvent {
  int64_t watch_id;
  bool active;
  std::string detail;
};

using DomainEvent = std::variant<OutputEvent, StoppedEvent, ContinuedEvent, ExitedEvent, ThreadExitedEvent,
                                 ProcessEvent, CapabilitiesEvent, InvalidatedEvent, MemoryEvent, ModuleEvent,
                                 BreakpointEvent, TerminatedEvent, TraceDataEvent, ResetEvent, TraceStatusEvent,
                                 WatchDataEvent, WatchStateEvent>;

class EventBus {
public:
  using Subscriber = std::function<void(const DomainEvent &)>;

  // subscriber must outlive the EventBus or removed before its destroyed
  void Subscribe(Subscriber subscriber) {
    std::lock_guard<std::mutex> guard(mutex_);
    subscribers_.push_back(std::move(subscriber));
  }

  // Publish() can be called from multiple threads (dispatch thread, LLDB
  // event thread, OpenOCD server_loop thread). Subscribers run on the
  // calling thread, under this lock - a subscriber must not call back into
  // Publish() or Subscribe().
  void Publish(const DomainEvent &event) const {
    std::lock_guard<std::mutex> guard(mutex_);
    for (const Subscriber &subscriber : subscribers_)
      subscriber(event);
  }

private:
  mutable std::mutex mutex_;
  std::vector<Subscriber> subscribers_;
};

}  // namespace core

#endif  // TRAILER_CORE_EVENT_BUS_HPP_
