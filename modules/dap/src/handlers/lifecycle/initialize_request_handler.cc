//===-- InitializeRequestHandler.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/target_manager.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/capabilities.hpp"
#include "handlers/request_handler.hpp"

using namespace dap;
using namespace dap::protocol;

/// Initialize request; value of command field is 'initialize'.
llvm::Expected<InitializeResponse> InitializeRequestHandler::Run(
    const InitializeRequestArguments &arguments) const {
  // Store initialization arguments for later use in Launch/Attach.
  orchestrator_.SetClientFeatures(arguments.supportedFeatures);
  context_.Session().source_init_file = arguments.lldbExtSourceInitFile;

  return AssembleCapabilities(orchestrator_, context_);
}
