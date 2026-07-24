//===-- DisassembleRequestHandler.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/disassembly_manager.hpp"
#include "debug_service/debug_service.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

/// Disassembles code stored at the provided location.
/// Clients should only call this request if the corresponding capability
/// `supportsDisassembleRequest` is true.
llvm::Expected<protocol::DisassembleResponseBody>
DisassembleRequestHandler::Run(const protocol::DisassembleArguments &args) const {
  llvm::Expected<std::vector<protocol::DisassembledInstruction>> instructions =
      dap.Context().Disassembly().Disassemble(args.memoryReference, args.offset, args.instructionOffset,
                                              args.instructionCount, args.resolveSymbols);
  if (!instructions)
    return instructions.takeError();

  return protocol::DisassembleResponseBody{std::move(*instructions)};
}

} // namespace dap
