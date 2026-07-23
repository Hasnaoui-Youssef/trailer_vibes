//===-- FunctionBreakpoint.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "debug_service/function_breakpoint.hpp"
#include "debug_service/debug_service.hpp"
#include "lldb/API/SBMutex.h"
#include <mutex>

namespace dap::debug_service {

FunctionBreakpoint::FunctionBreakpoint(
    DebugService &d, const protocol::FunctionBreakpoint &breakpoint)
    : Breakpoint(d, breakpoint.condition, breakpoint.hitCondition),
      m_function_name(breakpoint.name) {}

void FunctionBreakpoint::SetBreakpoint() {
  lldb::SBMutex lock = m_dap.GetAPIMutex();
  std::lock_guard<lldb::SBMutex> guard(lock);

  if (m_function_name.empty())
    return;
  m_bp = m_dap.target.BreakpointCreateByName(m_function_name.c_str());
  Breakpoint::SetBreakpoint();
}

} // namespace dap::debug_service
