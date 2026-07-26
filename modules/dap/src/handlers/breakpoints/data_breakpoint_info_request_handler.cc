//===-- DataBreakpointInfoRequestHandler.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/breakpoint_manager.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

/// Obtains information on a possible data breakpoint that could be set on an
/// expression or variable. Clients should only call this request if the
/// corresponding capability supportsDataBreakpoints is true.
llvm::Expected<protocol::DataBreakpointInfoResponseBody>
DataBreakpointInfoRequestHandler::Run(
    const protocol::DataBreakpointInfoArguments &args) const {
  return context_.Breakpoints().GetDataBreakpointInfo(args);
}

} // namespace dap
