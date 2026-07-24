//===-- SetVariableRequestHandler.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/execution_controller.hpp"
#include "core/components/target_manager.hpp"
#include "debug_service/debug_service.hpp"
#include "debug_service/json_utils.hpp"
#include "dap/protocol/protocol_events.hpp"
#include "handlers/request_handler.hpp"

using namespace dap::protocol;

namespace dap {

/// Set the variable with the given name in the variable container to a new
/// value. Clients should only call this request if the corresponding capability
/// `supportsSetVariable` is true.
///
/// If a debug adapter implements both `setVariable` and `setExpression`,
/// a client will only use `setExpression` if the variable has an evaluateName
/// property.
llvm::Expected<SetVariableResponseBody>
SetVariableRequestHandler::Run(const SetVariableArguments &args) const {
  const auto args_name = llvm::StringRef(args.name);

  if (args.variablesReference == UINT64_MAX) {
    return llvm::make_error<DAPError>(
        llvm::formatv("invalid reference {}", args.variablesReference).str(),
        llvm::inconvertibleErrorCode(),
        /*show_user=*/false);
  }

  constexpr llvm::StringRef return_value_name = "(Return Value)";
  if (args_name == return_value_name)
    return llvm::make_error<DAPError>(
        "cannot change the value of the return value");

  lldb::SBValue variable =
      dap.Context().Data().variables.FindVariable(args.variablesReference, args_name);

  if (!variable.IsValid())
    return llvm::make_error<DAPError>("could not find variable in scope");

  lldb::SBError error;
  const bool success = variable.SetValueFromCString(args.value.c_str(), error);
  if (!success)
    return llvm::make_error<DAPError>(error.GetCString());

  VariableDescription desc(variable,
                           dap.Context().Session().configuration.enableAutoVariableSummaries);

  SetVariableResponseBody body;
  body.value = desc.display_value;
  body.type = desc.display_type_name;

  // We don't know the index of the variable in our dap.variables
  // so always insert a new one to get its variablesReference.
  // is_permanent is false because debug console does not support
  // setVariable request.
  const int64_t new_var_ref =
      dap.Context().Data().variables.InsertVariable(variable, /*is_permanent=*/false);
  if (variable.MightHaveChildren()) {
    body.variablesReference = new_var_ref;
    if (desc.type_obj.IsArrayType())
      body.indexedVariables = variable.GetNumChildren();
    else
      body.namedVariables = variable.GetNumChildren();
  }

  if (const lldb::addr_t addr = variable.GetLoadAddress();
      addr != LLDB_INVALID_ADDRESS)
    body.memoryReference = addr;

  if (ValuePointsToCode(variable))
    body.valueLocationReference = new_var_ref;

  // Also send invalidated event to signal client that some variables
  // (e.g. references) can be changed.
  dap.Context().Execution().SendInvalidatedEvent({InvalidatedEventBody::eAreaVariables});

  // Also send memory event to signal client that variable memory was changed.
  dap.Context().Execution().SendMemoryEvent(variable);

  return body;
}

} // namespace dap
