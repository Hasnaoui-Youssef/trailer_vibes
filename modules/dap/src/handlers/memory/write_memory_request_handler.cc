//===-- WriteMemoryRequestHandler.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/execution_controller.hpp"
#include "core/components/memory_manager.hpp"
#include "dap/protocol/protocol_events.hpp"
#include "handlers/request_handler.hpp"
#include "llvm/Support/Base64.h"

using namespace dap::protocol;

namespace dap {

llvm::Expected<WriteMemoryResponseBody>
WriteMemoryRequestHandler::Run(const WriteMemoryArguments &args) const {
  const uint64_t address = args.memoryReference + args.offset;

  if (args.data.empty()) {
    return llvm::make_error<DAPError>(
        "Data cannot be empty value. Provide valid data");
  }

  // Client sends memory data as a Base64 string; decode before writing.
  std::vector<char> output;
  auto decode_error = llvm::decodeBase64(args.data, output);

  if (decode_error) {
    return llvm::make_error<DAPError>(
        llvm::toString(std::move(decode_error)).c_str());
  }

  llvm::Expected<core::MemoryWriteResult> result =
      context_.Memory().WriteMemory(address, output, args.allowPartial);
  if (!result)
    return result.takeError();

  WriteMemoryResponseBody response;
  response.bytesWritten = result->bytes_written;

  // Signals the client that some things (e.g. variables) may have changed.
  context_.Execution().SendInvalidatedEvent({InvalidatedEventBody::eAreaAll});

  return response;
}

} // namespace dap
