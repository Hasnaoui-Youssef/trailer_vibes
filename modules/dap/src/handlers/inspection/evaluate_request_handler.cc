//===-- EvaluateRequestHandler.cpp ----------------------------------------===//
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
#include "debug_service/lldb_utils.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/protocol/protocol_types.hpp"
#include "dap/protocol_support.hpp"
#include "handlers/request_handler.hpp"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

using namespace llvm;
using namespace dap::protocol;

namespace dap {

/// Evaluates the given expression in the context of a stack frame.
///
/// The expression has access to any variables and arguments that are in scope.
Expected<EvaluateResponseBody>
EvaluateRequestHandler::Run(const EvaluateArguments &arguments) const {
  EvaluateResponseBody body;
  lldb::SBFrame frame = dap.Context().GetLLDBFrame(arguments.frameId);
  std::string expression = arguments.expression;
  bool repeat_last_command =
      expression.empty() && dap.Context().Data().last_nonempty_var_expression.empty();

  if (arguments.context == protocol::eEvaluateContextRepl &&
      (repeat_last_command ||
       (!expression.empty() &&
        dap.Context().Data().DetectReplMode(frame, expression, false) == ReplMode::Command))) {
    // Since the current expression is not for a variable, clear the
    // last_nonempty_var_expression field.
    dap.Context().Data().last_nonempty_var_expression.clear();
    // If we're evaluating a command relative to the current frame, set the
    // focus_tid to the current frame for any thread related events.
    if (frame.IsValid()) {
      dap.Context().Execution().focus_tid = frame.GetThread().GetThreadID();
    }

    bool required_command_failed = false;
    body.result = RunLLDBCommands(
        dap.debugger, llvm::StringRef(), {expression}, required_command_failed,
        /*parse_command_directives=*/false, /*echo_commands=*/false);
    return body;
  }

  if (arguments.context == eEvaluateContextRepl) {
    // If the expression is empty and the last expression was for a
    // variable, set the expression to the previous expression (repeat the
    // evaluation); otherwise save the current non-empty expression for the
    // next (possibly empty) variable expression.
    if (expression.empty())
      expression = dap.Context().Data().last_nonempty_var_expression;
    else
      dap.Context().Data().last_nonempty_var_expression = expression;
  }

  // Always try to get the answer from the local variables if possible. If
  // this fails, then if the context is not "hover", actually evaluate an
  // expression using the expression parser.
  //
  // "frame variable" is more reliable than the expression parser in
  // many cases and it is faster.
  lldb::SBValue value = frame.GetValueForVariablePath(
      expression.data(), lldb::eDynamicDontRunTarget);

  // Freeze dry the value in case users expand it later in the debug console
  if (value.GetError().Success() && arguments.context == eEvaluateContextRepl)
    value = value.Persist();

  if (value.GetError().Fail() && arguments.context != eEvaluateContextHover)
    value = frame.EvaluateExpression(expression.data());

  if (value.GetError().Fail())
    return ToError(value.GetError(), /*show_user=*/false);

  const bool hex = arguments.format ? arguments.format->hex : false;

  VariableDescription desc(value, dap.Context().Session().configuration.enableAutoVariableSummaries,
                           hex);

  body.result = desc.GetResult(arguments.context);
  body.type = desc.display_type_name;

  if (value.MightHaveChildren() || ValuePointsToCode(value))
    body.variablesReference = dap.Context().Data().variables.InsertVariable(
        value, /*is_permanent=*/arguments.context == eEvaluateContextRepl);

  if (lldb::addr_t addr = value.GetLoadAddress(); addr != LLDB_INVALID_ADDRESS)
    body.memoryReference = EncodeMemoryReference(addr);

  if (ValuePointsToCode(value) &&
      body.variablesReference != LLDB_DAP_INVALID_VAR_REF)
    body.valueLocationReference = PackLocation(body.variablesReference, true);

  return body;
}

} // namespace dap
