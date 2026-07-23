//===-- InitializeRequestHandler.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "debug_service/debug_service.hpp"
#include "debug_service/event_helper.hpp"
#include "debug_service/json_utils.hpp"
#include "debug_service/lldb_utils.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "debug_service/request_handler.hpp"
#include "lldb/API/SBTarget.h"

using namespace dap::debug_service;
using namespace dap::protocol;

/// Initialize request; value of command field is 'initialize'.
llvm::Expected<InitializeResponse> InitializeRequestHandler::Run(
    const InitializeRequestArguments &arguments) const {
  // Store initialization arguments for later use in Launch/Attach.
  dap.clientFeatures = arguments.supportedFeatures;
  dap.sourceInitFile = arguments.lldbExtSourceInitFile;

  return dap.GetCapabilities();
}
