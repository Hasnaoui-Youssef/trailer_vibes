//===-- Breakpoint.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_BREAKPOINT_H
#define LLDB_TOOLS_LLDB_DAP_BREAKPOINT_H

#include "debug_service/breakpoint_base.hpp"
#include "debug_service/dap_forward.hpp"
#include "lldb/API/SBBreakpoint.h"

namespace dap::debug_service {

class Breakpoint : public BreakpointBase {
public:
  Breakpoint(DebugService &d, const std::optional<std::string> &condition,
             const std::optional<std::string> &hit_condition)
      : BreakpointBase(d, condition, hit_condition) {}
  Breakpoint(DebugService &d, lldb::SBBreakpoint bp) : BreakpointBase(d), m_bp(bp) {}

  lldb::break_id_t GetID() const { return m_bp.GetID(); }

  void SetCondition() override;
  void SetHitCondition() override;
  protocol::Breakpoint ToProtocolBreakpoint() override;

  bool MatchesName(const char *name);
  void SetBreakpoint();

protected:
  /// The LLDB breakpoint associated wit this source breakpoint.
  lldb::SBBreakpoint m_bp;
};
} // namespace dap::debug_service

#endif
