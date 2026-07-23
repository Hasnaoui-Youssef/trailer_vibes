//===-- EventHelper.h -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_EVENTHELPER_H
#define LLDB_TOOLS_LLDB_DAP_EVENTHELPER_H

#include "debug_service/dap_forward.hpp"
#include "dap/protocol/protocol_events.hpp"
#include "lldb/API/SBValue.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

namespace dap::debug_service {
struct DebugService;

enum LaunchMethod { Launch, Attach, AttachForSuspendedLaunch };

/// Sends target based capabilities and lldb-dap custom capabilities.
void SendExtraCapabilities(DebugService &dap);

void SendProcessEvent(DebugService &dap, LaunchMethod launch_method);

llvm::Error SendThreadStoppedEvent(DebugService &dap, bool on_entry = false);

void SendTerminatedEvent(DebugService &dap);

void SendStdOutStdErr(DebugService &dap, lldb::SBProcess &process);

void SendContinuedEvent(DebugService &dap);

void SendProcessExitedEvent(DebugService &dap, lldb::SBProcess &process);

void SendInvalidatedEvent(
    DebugService &dap, llvm::ArrayRef<protocol::InvalidatedEventBody::Area> areas,
    lldb::tid_t tid = LLDB_INVALID_THREAD_ID);

void SendMemoryEvent(DebugService &dap, lldb::SBValue variable);

/// Runs the async SBListener-based event loop for `dap`'s session (process/
/// target/breakpoint/thread state changes -> proactive `stopped`/
/// `continued`/`output`/etc DAP events). Blocks until `dap.broadcaster`
/// receives eBroadcastBitStopEventThread. Intended to be run on its own
/// std::thread - see DebugService::StartEventThread.
void EventThread(DebugService &dap);

} // namespace dap::debug_service

#endif
