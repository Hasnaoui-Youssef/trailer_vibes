//===-- CompileUnitsRequestHandler.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "debug_service/debug_service.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"
#include <climits>  // PATH_MAX - lldb/Host/PosixApi.h is lldb_private, off-limits here

using namespace dap;
using namespace dap::protocol;

static CompileUnit CreateCompileUnit(lldb::SBCompileUnit &unit) {
  char unit_path_arr[PATH_MAX];
  unit.GetFileSpec().GetPath(unit_path_arr, sizeof(unit_path_arr));
  std::string unit_path(unit_path_arr);
  return {std::move(unit_path)};
}

/// The `compileUnits` request returns an array of path of compile units for
/// given module specified by `moduleId`.
llvm::Expected<CompileUnitsResponseBody> CompileUnitsRequestHandler::Run(
    const std::optional<CompileUnitsArguments> &args) const {
  std::vector<CompileUnit> units;
  int num_modules = dap.target.GetNumModules();
  for (int i = 0; i < num_modules; i++) {
    auto curr_module = dap.target.GetModuleAtIndex(i);
    if (args->moduleId == llvm::StringRef(curr_module.GetUUIDString())) {
      int num_units = curr_module.GetNumCompileUnits();
      for (int j = 0; j < num_units; j++) {
        auto curr_unit = curr_module.GetCompileUnitAtIndex(j);
        units.emplace_back(CreateCompileUnit(curr_unit));
      }
      break;
    }
  }
  return CompileUnitsResponseBody{std::move(units)};
}
