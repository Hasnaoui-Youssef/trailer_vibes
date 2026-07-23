//===-- CommandPlugins.h --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_COMMANDPLUGINS_H
#define LLDB_TOOLS_LLDB_DAP_COMMANDPLUGINS_H

#include "debug_service/debug_service.hpp"
#include "lldb/API/SBCommandInterpreter.h"

namespace dap::debug_service {

struct StartDebuggingCommand : public lldb::SBCommandPluginInterface {
  DebugService &dap;
  explicit StartDebuggingCommand(DebugService &d) : dap(d) {};
  bool DoExecute(lldb::SBDebugger debugger, char **command,
                 lldb::SBCommandReturnObject &result) override;
};

struct ReplModeCommand : public lldb::SBCommandPluginInterface {
  DebugService &dap;
  explicit ReplModeCommand(DebugService &d) : dap(d) {};
  bool DoExecute(lldb::SBDebugger debugger, char **command,
                 lldb::SBCommandReturnObject &result) override;
};

struct SendEventCommand : public lldb::SBCommandPluginInterface {
  DebugService &dap;
  explicit SendEventCommand(DebugService &d) : dap(d) {};
  bool DoExecute(lldb::SBDebugger debugger, char **command,
                 lldb::SBCommandReturnObject &result) override;
};

} // namespace dap::debug_service

#endif
