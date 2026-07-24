//===-- event_bus.hpp --------------------------------------------------===//
//
// Observer/event-bus for domain events flowing out of core (see CLAUDE.md's
// DebugContext architecture). Components publish; dap_handlers'
// event_translator is the sole subscriber, converting each domain event to
// the matching DAP protocol event and sending it via the Orchestrator -
// this is the only place a domain event meets the wire. core itself never
// touches Transport/Orchestrator.
//
// DomainEvent is grown incrementally as components are carved out of
// DebugService: OutputEvent lands with the Breakpoint carve (it needs to
// route SendOutput-equivalent calls without depending on debug_service);
// Stopped/Continued/Exited/etc. land with the Execution carve, which owns
// the SB event pump.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_CORE_EVENT_BUS_HPP_
#define TRAILER_CORE_EVENT_BUS_HPP_

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "dap/protocol/protocol_base.hpp"
#include "llvm/Support/JSON.h"

namespace core {

enum class OutputCategory { Console, Important, Stdout, Stderr, Telemetry };

struct OutputEvent {
  OutputCategory category;
  std::string text;
};

// A fully-built DAP event, still carrying the `kCalculateSeq` placeholder
// seq - matches exactly what debug_service::DebugService::Send/SendJSON
// accept today (see DebugService::Send: it mutates `.seq` via
// Orchestrator::NextSeq() immediately before serializing). The
// event_translator (dap_handlers) is what actually assigns the real seq and
// writes the message to the transport - core never touches Transport/
// Orchestrator itself. Carrying the already-typed protocol::Message (rather
// than a bespoke struct per DAP event kind) means every one of
// DebugService's existing Send()/SendJSON() call sites relocates verbatim
// instead of needing a new typed event + translator arm per DAP event kind.
struct WireMessageEvent {
  dap::protocol::Message message;
};

using DomainEvent = std::variant<OutputEvent, WireMessageEvent>;

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
