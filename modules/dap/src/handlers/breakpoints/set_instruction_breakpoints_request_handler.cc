//===-- SetInstructionBreakpointsRequestHandler.cpp -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/breakpoint_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

/// Replaces all existing instruction breakpoints. Typically, instruction
/// breakpoints would be set from a disassembly window. To clear all instruction
/// breakpoints, specify an empty array. When an instruction breakpoint is hit,
/// a stopped event (with reason instruction breakpoint) is generated. Clients
/// should only call this request if the corresponding capability
/// supportsInstructionBreakpoints is true.
llvm::Expected<protocol::SetInstructionBreakpointsResponseBody>
SetInstructionBreakpointsRequestHandler::Run(
    const protocol::SetInstructionBreakpointsArguments &args) const {
  return context_.Breakpoints().SetInstructionBreakpoints(args);
}

} // namespace dap
