//===-- variables.hpp -----------------------------------------------------===//
//
// ScopeKind, ScopeData, FrameScopes, Variables and CreateScope moved to
// core/components/data_manager.hpp with DataManager (see
// PROJECT_STATUS.md's Data carve) - they only ever touched lldb::SBValue*
// and protocol::Scope, no DebugService coupling. Re-exported here so
// existing callers keep working unqualified.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_VARIABLES_H
#define LLDB_TOOLS_LLDB_DAP_VARIABLES_H

#include "core/components/data_manager.hpp"

namespace dap {

using core::CreateScope;
using core::eScopeKindGlobals;
using core::eScopeKindLocals;
using core::eScopeKindRegisters;
using core::FrameScopes;
using core::ScopeData;
using core::ScopeKind;
using core::Variables;

} // namespace dap

#endif
