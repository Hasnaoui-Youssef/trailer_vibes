//===-- CompileUnitsRequestHandler.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/module_manager.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

using namespace dap;
using namespace dap::protocol;

llvm::Expected<CompileUnitsResponseBody> CompileUnitsRequestHandler::Run(
    const std::optional<CompileUnitsArguments> &args) const {
  return context_.Modules().GetCompileUnitsRequest(args);
}
