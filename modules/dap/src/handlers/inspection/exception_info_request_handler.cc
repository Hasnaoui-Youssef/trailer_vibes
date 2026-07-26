//===-- ExceptionInfoRequestHandler.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/execution_controller.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::ExceptionInfoResponseBody>
ExceptionInfoRequestHandler::Run(const protocol::ExceptionInfoArguments &args) const {
  return context_.Execution().GetExceptionInfoRequest(args);
}
} // namespace dap
