//===-- ReadMemoryRequestHandler.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/memory_manager.hpp"
#include "handlers/request_handler.hpp"

namespace dap {

llvm::Expected<protocol::ReadMemoryResponseBody>
ReadMemoryRequestHandler::Run(const protocol::ReadMemoryArguments &args) const {
  const uint64_t raw_address = args.memoryReference + args.offset;

  llvm::Expected<core::MemoryReadResult> result = context_.Memory().ReadMemory(raw_address, args.count);
  if (!result)
    return result.takeError();

  protocol::ReadMemoryResponseBody response;
  response.data = std::move(result->data);
  response.address = result->address;
  if (result->unreadable_bytes)
    response.unreadableBytes = *result->unreadable_bytes;
  return response;
}

} // namespace dap
