//===-- BreakpointLocationsHandler..cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/breakpoint_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

/// The `breakpointLocations` request returns all possible locations for source
/// breakpoints in a given range. Clients should only call this request if the
/// corresponding capability `supportsBreakpointLocationsRequest` is true.
llvm::Expected<protocol::BreakpointLocationsResponseBody>
BreakpointLocationsRequestHandler::Run(
    const protocol::BreakpointLocationsArguments &args) const {
  return context_.Breakpoints().GetBreakpointLocations(args);
}

} // namespace dap
