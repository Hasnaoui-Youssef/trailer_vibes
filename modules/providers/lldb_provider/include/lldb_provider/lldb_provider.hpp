#ifndef TRAILER_PROVIDERS_LLDB_PROVIDER_HPP_
#define TRAILER_PROVIDERS_LLDB_PROVIDER_HPP_

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBMutex.h"
#include "lldb/API/SBTarget.h"

namespace providers {

// Owns the LLDB SB API session state for one debug session: the SBDebugger
// and its currently selected SBTarget. This is the engine's LLDB backend
// integration - the lowest layer, with no abstraction beneath it (see root
// CLAUDE.md's provider model). Components (and, transitionally,
// DebugService) hold a reference to one of these rather than owning their
// own SBDebugger.
//
// Deliberately minimal: SBProcess, the command interpreter, and the
// SBListener aren't stored separately, since the SB API already exposes
// them as cheap accessors off SBTarget/SBDebugger (`target.GetProcess()`,
// `debugger.GetCommandInterpreter()`, `debugger.GetListener()`) - there is
// nothing to additionally own beyond the two root objects those hang off.
class LldbProvider {
public:
    LldbProvider() = default;

    LldbProvider(const LldbProvider &) = delete;
    LldbProvider &operator=(const LldbProvider &) = delete;

    lldb::SBDebugger debugger;
    lldb::SBTarget target;

    // Serializes access to the debugger/target across threads (the DAP
    // request-handling thread and the event thread) - mirrors lldb-dap's
    // own locking discipline.
    lldb::SBMutex GetAPIMutex() const { return target.GetAPIMutex(); }
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_LLDB_PROVIDER_HPP_
