//===-- SetBreakpointsRequestHandler.cpp ----------------------------------===//
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

llvm::Expected<protocol::SetBreakpointsResponseBody>
SetBreakpointsRequestHandler::Run(
    const protocol::SetBreakpointsArguments &args) const {
  std::vector<protocol::Breakpoint> response_breakpoints =
      context_.Breakpoints().SetSourceBreakpoints(args.source, args.breakpoints);
  return protocol::SetBreakpointsResponseBody{std::move(response_breakpoints)};
}

} // namespace dap
