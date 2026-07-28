#ifndef TRAILER_CORE_EVENT_BUS_HPP_
#define TRAILER_CORE_EVENT_BUS_HPP_

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "dap/protocol/protocol_events.hpp"
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

using DomainEvent = std::variant<OutputEvent, StoppedEvent, ContinuedEvent, ExitedEvent, ThreadExitedEvent,
                                 ProcessEvent, CapabilitiesEvent, InvalidatedEvent, MemoryEvent, ModuleEvent,
                                 BreakpointEvent, TerminatedEvent, TraceDataEvent, ResetEvent>;

class EventBus {
public:
  using Subscriber = std::function<void(const DomainEvent &)>;

  // subscriber must outlive the EventBus or removed before its destroyed
  void Subscribe(Subscriber subscriber) { subscribers_.push_back(std::move(subscriber)); }

  void Publish(const DomainEvent &event) const {
    for (const Subscriber &subscriber : subscribers_)
      subscriber(event);
  }

private:
  std::vector<Subscriber> subscribers_;
};

}  // namespace core

#endif  // TRAILER_CORE_EVENT_BUS_HPP_
