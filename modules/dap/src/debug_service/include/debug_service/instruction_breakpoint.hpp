//===-- InstructionBreakpoint.h --------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_INSTRUCTIONBREAKPOINT_H
#define LLDB_TOOLS_LLDB_DAP_INSTRUCTIONBREAKPOINT_H

#include "debug_service/breakpoint.hpp"
#include "debug_service/dap_forward.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "lldb/lldb-types.h"
#include <cstdint>

namespace dap::debug_service {

/// Instruction Breakpoint
class InstructionBreakpoint : public Breakpoint {
public:
  InstructionBreakpoint(DebugService &d,
                        const protocol::InstructionBreakpoint &breakpoint);

  /// Set instruction breakpoint in LLDB as a new breakpoint.
  void SetBreakpoint();

  lldb::addr_t GetInstructionAddressReference() const {
    return m_instruction_address_reference;
  }

protected:
  lldb::addr_t m_instruction_address_reference;
  int32_t m_offset;
};

} // namespace dap::debug_service

#endif
