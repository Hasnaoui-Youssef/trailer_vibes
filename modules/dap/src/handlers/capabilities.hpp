//===-- capabilities.hpp ----------------------------------------------===//
//
// Assembles the `initialize` response's Capabilities (see CLAUDE.md's DAP
// layer) from the Orchestrator (communication-layer feature aggregation)
// and core::DebugContext (target/LLDB-derived pieces: exception-breakpoint
// filters, the LLDB version string) - this only ever happens dap->core, so
// it lives here rather than on DebugContext itself (which would make core
// depend on a purely dap-layer concern).
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_DAP_HANDLERS_CAPABILITIES_HPP_
#define TRAILER_DAP_HANDLERS_CAPABILITIES_HPP_

#include "core/debug_context.hpp"
#include "dap/orchestrator.hpp"
#include "dap/protocol/protocol_types.hpp"

namespace dap {

protocol::Capabilities AssembleCapabilities(Orchestrator &orchestrator, core::DebugContext &context);

// The subset of capabilities ExecutionController re-announces once a target
// is known (see ExecutionController::SendExtraCapabilities, which reaches
// this via the DebugContext::GetCustomCapabilities seam - the one
// core->dap direction this file's assembly logic is called from).
protocol::Capabilities AssembleCustomCapabilities(Orchestrator &orchestrator);

}  // namespace dap

#endif  // TRAILER_DAP_HANDLERS_CAPABILITIES_HPP_
