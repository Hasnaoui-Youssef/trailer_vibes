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

// The "terminated" event's body is `{"$__lldb_statistics": <dump>}.
struct TerminatedEvent {
  std::string statistics_json;
};

using DomainEvent = std::variant<OutputEvent, StoppedEvent, ContinuedEvent, ExitedEvent, ThreadExitedEvent,
                                 ProcessEvent, CapabilitiesEvent, InvalidatedEvent, MemoryEvent, ModuleEvent,
                                 BreakpointEvent, TerminatedEvent>;

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
