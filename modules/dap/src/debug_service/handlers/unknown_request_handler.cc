//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "debug_service/dap_error.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "debug_service/request_handler.hpp"
#include "llvm/Support/Error.h"

using namespace dap::debug_service;
using namespace dap::protocol;

llvm::Error UnknownRequestHandler::Run(const UnknownArguments &args) const {
  return llvm::make_error<DAPError>("unknown request");
}
