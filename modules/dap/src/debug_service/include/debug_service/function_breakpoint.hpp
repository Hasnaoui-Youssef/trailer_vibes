//===-- FunctionBreakpoint.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_FUNCTIONBREAKPOINT_H
#define LLDB_TOOLS_LLDB_DAP_FUNCTIONBREAKPOINT_H

#include "debug_service/breakpoint.hpp"
#include "debug_service/dap_forward.hpp"
#include "dap/protocol/protocol_types.hpp"

namespace dap::debug_service {

class FunctionBreakpoint : public Breakpoint {
public:
  FunctionBreakpoint(DebugService &dap, const protocol::FunctionBreakpoint &breakpoint);

  /// Set this breakpoint in LLDB as a new breakpoint.
  void SetBreakpoint();

  llvm::StringRef GetFunctionName() const { return m_function_name; }

protected:
  std::string m_function_name;
};

} // namespace dap::debug_service

#endif
