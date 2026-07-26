//===-- event_bus.hpp --------------------------------------------------===//
//
// Observer/event-bus for domain events flowing out of core (see CLAUDE.md's
// DebugContext architecture). Components publish; dap_handlers'
// event_translator is the sole subscriber, converting each domain event to
// the matching DAP protocol event and sending it via the Orchestrator -
// this is the only place a domain event meets the wire. core itself never
// touches Transport/Orchestrator, and (see each event struct below) never
// builds a llvm::json::Value or a dap::protocol::Message either - every
// DomainEvent alternative is a plain-data payload (or an already-typed
// dap::protocol::*EventBody, which is plain data itself - see
// dap/protocol/protocol_events.hpp); only the event_translator ever calls
// toJSON on one.
//
// DomainEvent is grown incrementally as components are carved out of
// DebugService: OutputEvent lands with the Breakpoint carve (it needs to
// route SendOutput-equivalent calls without depending on debug_service);
// the rest land with the Execution carve, which owns the SB event pump -
// one alternative per DAP event kind ExecutionController/TargetManager
// emit.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_EVENT_BUS_HPP_
#define TRAILER_CORE_EVENT_BUS_HPP_

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "dap/protocol/protocol_events.hpp"

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

// The "terminated" event's body is `{"$__lldb_statistics": <dump>}`, where
// <dump> is an arbitrary, dynamically-shaped blob straight from LLDB's
// SBStructuredData (see core::BuildTerminatedStatisticsJSON) - there's no
// fixed protocol struct to give it. Carried across as an already-serialized
// JSON string (plain data, not a llvm::json type) rather than inventing a
// parallel dynamic-value type; the event_translator re-parses it once, only
// when building the wire event. Empty if the target had no statistics.
struct TerminatedEvent {
  std::string statistics_json;
};

using DomainEvent = std::variant<OutputEvent, StoppedEvent, ContinuedEvent, ExitedEvent, ThreadExitedEvent,
                                 ProcessEvent, CapabilitiesEvent, InvalidatedEvent, MemoryEvent, ModuleEvent,
                                 BreakpointEvent, TerminatedEvent>;

class EventBus {
public:
  using Subscriber = std::function<void(const DomainEvent &)>;

  // `subscriber` must outlive the EventBus (or be removed before it is
  // destroyed - there is no unsubscribe yet, since today there is exactly
  // one long-lived subscriber: dap_handlers' event_translator).
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
