//===-- ProtocolEvents.h --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains POD structs based on the DAP specification at
// https://microsoft.github.io/debug-adapter-protocol/specification
//
// This is not meant to be a complete implementation, new interfaces are added
// when they're needed.
//
// Each struct has a toJSON and fromJSON function, that converts between
// the struct and a JSON representation. (See JSON.h)
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_DAP_PROTOCOL_PROTOCOL_EVENTS_HPP_
#define TRAILER_DAP_PROTOCOL_PROTOCOL_EVENTS_HPP_

#include "dap/protocol/protocol_types.hpp"
#include "dap/protocol/dap_defines.hpp"
#include "llvm/Support/JSON.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace dap::protocol {

/// The event indicates that one or more capabilities have changed.
///
/// Since the capabilities are dependent on the client and its UI, it might not
/// be possible to change that at random times (or too late).
///
/// Consequently this event has a hint characteristic: a client can only be
/// expected to make a 'best effort' in honoring individual capabilities but
/// there are no guarantees.
///
/// Only changed capabilities need to be included, all other capabilities keep
/// their values.
struct CapabilitiesEventBody {
  Capabilities capabilities;
};
llvm::json::Value toJSON(const CapabilitiesEventBody &);

/// The event indicates that some information about a module has changed.
struct ModuleEventBody {
  enum Reason : unsigned { eReasonNew, eReasonChanged, eReasonRemoved };

  /// The new, changed, or removed module. In case of `removed` only the module
  /// id is used.
  Module module;

  /// The reason for the event.
  /// Values: 'new', 'changed', 'removed'
  Reason reason;
};
llvm::json::Value toJSON(const ModuleEventBody::Reason &);
llvm::json::Value toJSON(const ModuleEventBody &);

/// This event signals that some state in the debug adapter has changed and
/// requires that the client needs to re-render the data snapshot previously
/// requested.
///
/// Debug adapters do not have to emit this event for runtime changes like
/// stopped or thread events because in that case the client refetches the new
/// state anyway. But the event can be used for example to refresh the UI after
/// rendering formatting has changed in the debug adapter.
///
/// This event should only be sent if the corresponding capability
/// supportsInvalidatedEvent is true.
struct InvalidatedEventBody {
  enum Area : unsigned { eAreaAll, eAreaStacks, eAreaThreads, eAreaVariables };

  /// Set of logical areas that got invalidated.
  std::vector<Area> areas;

  /// If specified, the client only needs to refetch data related to this
  /// thread.
  std::optional<tid_t> threadId;

  /// If specified, the client only needs to refetch data related to this stack
  /// frame (and the `threadId` is ignored).
  std::optional<uint64_t> stackFrameId;
};
llvm::json::Value toJSON(const InvalidatedEventBody::Area &);
llvm::json::Value toJSON(const InvalidatedEventBody &);

/// This event indicates that some memory range has been updated. It should only
/// be sent if the corresponding capability supportsMemoryEvent is true.
///
/// Clients typically react to the event by re-issuing a readMemory request if
/// they show the memory identified by the memoryReference and if the updated
/// memory range overlaps the displayed range. Clients should not make
/// assumptions how individual memory references relate to each other, so they
/// should not assume that they are part of a single continuous address range
/// and might overlap.
///
/// Debug adapters can use this event to indicate that the contents of a memory
/// range has changed due to some other request like setVariable or
/// setExpression. Debug adapters are not expected to emit this event for each
/// and every memory change of a running program, because that information is
/// typically not available from debuggers and it would flood clients with too
/// many events.
struct MemoryEventBody {
  /// Memory reference of a memory range that has been updated.
  addr_t memoryReference = LLDB_INVALID_ADDRESS;

  /// Starting offset in bytes where memory has been updated. Can be negative.
  int64_t offset = 0;

  /// Number of bytes updated.
  uint64_t count = 0;
};
llvm::json::Value toJSON(const MemoryEventBody &);

/// The event indicates that the target has produced some output.
struct ProcessEventBody {
  /// The logical name of the process. This is usually the full path to
  /// process's executable file.
  std::string name;

  /// The system process id of the debugged process.
  pid_t systemProcessId = LLDB_INVALID_PROCESS_ID;

  /// If true, the process is running on the same computer as the debug
  /// adapter.
  bool isLocalProcess = false;

  /// The size of a pointer or address for this process, in bits.
  uint64_t pointerSize = 0;

  /// Describes how the debug engine started debugging this process.
  /// Values: 'launch', 'attach', 'attachForSuspendedLaunch'
  std::string startMethod;
};
llvm::json::Value toJSON(const ProcessEventBody &);

/// The event indicates that a thread has exited (the only `reason` this
/// adapter ever sends for the `thread` event).
struct ThreadExitedEventBody {
  tid_t threadId = LLDB_INVALID_THREAD_ID;
};
llvm::json::Value toJSON(const ThreadExitedEventBody &);

/// The event indicates that the execution of the debuggee has stopped due to
/// some condition (e.g. a breakpoint was hit, a step completed).
struct StoppedEventBody {
  /// The reason for the event.
  /// Values: 'step', 'breakpoint', 'exception', 'pause', 'entry', 'goto',
  /// 'function breakpoint', 'data breakpoint', 'instruction breakpoint', etc.
  std::string reason;

  /// Additional information, e.g. the address that was hit for an
  /// instruction breakpoint, or the exception's label.
  std::optional<std::string> description;

  /// The thread which was stopped.
  std::optional<tid_t> threadId;

  /// Ids of the breakpoints that triggered the event, if any.
  std::optional<std::vector<int64_t>> hitBreakpointIds;

  /// Set only when true - used in tests to validate breaking behavior.
  bool threadCausedFocus = false;

  /// If true, the client should not automatically bring the focused thread's
  /// UI into focus, but only take it into account when refreshing.
  bool preserveFocusHint = false;

  /// If true, all threads were stopped (as opposed to a single thread).
  bool allThreadsStopped = true;
};
llvm::json::Value toJSON(const StoppedEventBody &);

/// The event indicates that the execution of the debuggee has continued.
struct ContinuedEventBody {
  /// The thread which was continued.
  tid_t threadId = LLDB_INVALID_THREAD_ID;

  /// If true, all threads were continued (as opposed to a single thread).
  bool allThreadsContinued = true;
};
llvm::json::Value toJSON(const ContinuedEventBody &);

/// The event indicates that the debuggee has exited and returns its exit
/// code.
struct ExitedEventBody {
  int64_t exitCode = 0;
};
llvm::json::Value toJSON(const ExitedEventBody &);

/// The event indicates that some information about a breakpoint has
/// changed. This adapter only ever reports the `changed` reason (locations
/// added/removed/resolved on a breakpoint set through DAP don't remove the
/// breakpoint itself).
struct BreakpointEventBody {
  Breakpoint breakpoint;
};
llvm::json::Value toJSON(const BreakpointEventBody &);

} // end namespace dap::protocol

#endif  // TRAILER_DAP_PROTOCOL_PROTOCOL_EVENTS_HPP_
