//===-- SourceRequestHandler.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/disassembly_manager.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::SourceResponseBody>
SourceRequestHandler::Run(const protocol::SourceArguments &args) const {
  return context_.Disassembly().GetSourceRequest(args);
}

} // namespace dap
