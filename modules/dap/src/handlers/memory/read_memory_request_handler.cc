//===-- ReadMemoryRequestHandler.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/memory_manager.hpp"
#include "debug_service/debug_service.hpp"
#include "debug_service/json_utils.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

// Reads bytes from memory at the provided location.
//
// Clients should only call this request if the corresponding capability
// `supportsReadMemoryRequest` is true
llvm::Expected<protocol::ReadMemoryResponseBody>
ReadMemoryRequestHandler::Run(const protocol::ReadMemoryArguments &args) const {
  const lldb::addr_t raw_address = args.memoryReference + args.offset;

  lldb::SBProcess process = dap.target.GetProcess();
  if (!lldb::SBDebugger::StateIsStoppedState(process.GetState()))
    return llvm::make_error<NotStoppedError>();

  core::MemoryReadResult result = dap.Context().Memory().ReadMemory(raw_address, args.count);

  protocol::ReadMemoryResponseBody response;
  response.data = std::move(result.data);
  response.address = result.address;
  if (result.unreadable_bytes)
    response.unreadableBytes = *result.unreadable_bytes;
  return response;
}

} // namespace dap
