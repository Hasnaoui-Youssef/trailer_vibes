//===-- LocationsRequestHandler.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/lldb_utils.hpp"
#include "debug_service/debug_service.hpp"
#include "dap/dap_error.hpp"
#include "debug_service/json_utils.hpp"
#include "debug_service/lldb_utils.hpp"
#include "handlers/request_handler.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDeclaration.h"
#include "lldb/API/SBLineEntry.h"

namespace dap {

// Looks up information about a location reference previously returned by the
// debug adapter.
llvm::Expected<protocol::LocationsResponseBody>
LocationsRequestHandler::Run(const protocol::LocationsArguments &args) const {
  protocol::LocationsResponseBody response;
  // We use the lowest bit to distinguish between value location and declaration
  // location
  auto [var_ref, is_value_location] = UnpackLocation(args.locationReference);
  lldb::SBValue variable = dap.Context().Data().variables.GetVariable(var_ref);
  if (!variable.IsValid())
    return llvm::make_error<DAPError>("Invalid variable reference");

  if (is_value_location) {
    // Get the value location
    if (!variable.GetType().IsPointerType() &&
        !variable.GetType().IsReferenceType())
      return llvm::make_error<DAPError>(
          "Value locations are only available for pointers and references");

    lldb::addr_t raw_addr = variable.GetValueAsAddress();
    lldb::SBAddress addr = dap.target.ResolveLoadAddress(raw_addr);
    lldb::SBLineEntry line_entry = GetLineEntryForAddress(dap.target, addr);

    if (!line_entry.IsValid())
      return llvm::make_error<DAPError>(
          "Failed to resolve line entry for location");

    std::optional<protocol::Source> source =
        core::CreateSource(line_entry.GetFileSpec());
    if (!source)
      return llvm::make_error<DAPError>(
          "Failed to resolve file path for location");

    response.source = std::move(*source);
    response.line = line_entry.GetLine();
    response.column = line_entry.GetColumn();
  } else {
    // Get the declaration location
    lldb::SBDeclaration decl = variable.GetDeclaration();
    if (!decl.IsValid())
      return llvm::make_error<DAPError>("No declaration location available");

    std::optional<protocol::Source> source = core::CreateSource(decl.GetFileSpec());
    if (!source)
      return llvm::make_error<DAPError>(
          "Failed to resolve file path for location");

    response.source = std::move(*source);
    response.line = decl.GetLine();
    response.column = decl.GetColumn();
  }

  return response;
}

} // namespace dap
