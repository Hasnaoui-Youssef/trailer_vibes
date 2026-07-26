//===-- SetExceptionBreakpointsRequestHandler.cpp -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/breakpoint_manager.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"
#include <set>

using namespace llvm;
using namespace dap::protocol;

namespace dap {

Expected<SetExceptionBreakpointsResponseBody>
SetExceptionBreakpointsRequestHandler::Run(
    const SetExceptionBreakpointsArguments &arguments) const {
  core::BreakpointManager &breakpoints = context_.Breakpoints();

  // Keep a list of any exception breakpoint filter names that weren't set
  // so we can clear any exception breakpoints if needed.
  std::set<StringRef> unset_filters;
  for (const auto &bp : breakpoints.exception_breakpoints)
    unset_filters.insert(bp.GetFilter());

  SetExceptionBreakpointsResponseBody body;
  for (const auto &filter : arguments.filters) {
    auto *exc_bp = breakpoints.GetExceptionBreakpoint(filter);
    if (!exc_bp)
      continue;

    body.breakpoints.push_back(exc_bp->SetBreakpoint());
    unset_filters.erase(filter);
  }
  for (const auto &filterOptions : arguments.filterOptions) {
    auto *exc_bp = breakpoints.GetExceptionBreakpoint(filterOptions.filterId);
    if (!exc_bp)
      continue;

    body.breakpoints.push_back(exc_bp->SetBreakpoint(filterOptions.condition));
    unset_filters.erase(filterOptions.filterId);
  }

  // Clear any unset filters.
  for (const auto &filter : unset_filters) {
    auto *exc_bp = breakpoints.GetExceptionBreakpoint(filter);
    if (!exc_bp)
      continue;

    exc_bp->ClearBreakpoint();
  }

  return body;
}

} // namespace dap
