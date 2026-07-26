//===-- SetDataBreakpointsRequestHandler.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/breakpoint_manager.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

/// Replaces all existing data breakpoints with new data breakpoints.
/// To clear all data breakpoints, specify an empty array.
/// When a data breakpoint is hit, a stopped event (with reason data breakpoint)
/// is generated. Clients should only call this request if the corresponding
/// capability supportsDataBreakpoints is true.
llvm::Expected<protocol::SetDataBreakpointsResponseBody>
SetDataBreakpointsRequestHandler::Run(
    const protocol::SetDataBreakpointsArguments &args) const {
  return context_.Breakpoints().SetDataBreakpoints(args);
}

} // namespace dap
