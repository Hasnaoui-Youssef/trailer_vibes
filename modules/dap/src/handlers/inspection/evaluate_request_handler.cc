//===-- EvaluateRequestHandler.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/data_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::EvaluateResponseBody>
EvaluateRequestHandler::Run(const protocol::EvaluateArguments &arguments) const {
  return context_.Data().GetEvaluateRequest(arguments);
}

} // namespace dap
