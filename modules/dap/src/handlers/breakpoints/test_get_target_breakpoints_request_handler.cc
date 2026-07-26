//===-- TestGetTargetBreakpointsRequestHandler.cpp ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/breakpoint.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

using namespace dap;
using namespace dap::protocol;

llvm::Expected<TestGetTargetBreakpointsResponseBody>
TestGetTargetBreakpointsRequestHandler::Run(
    const TestGetTargetBreakpointsArguments &args) const {
  std::vector<protocol::Breakpoint> breakpoints;
  for (uint32_t i = 0; context_.Target().GetBreakpointAtIndex(i).IsValid(); ++i) {
    auto bp = core::Breakpoint(context_, context_.Target().GetBreakpointAtIndex(i));
    breakpoints.push_back(bp.ToProtocolBreakpoint());
  }
  return TestGetTargetBreakpointsResponseBody{std::move(breakpoints)};
}
