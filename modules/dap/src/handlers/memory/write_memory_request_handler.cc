//===-- WriteMemoryRequestHandler.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "core/components/execution_controller.hpp"
#include "core/components/memory_manager.hpp"
#include "debug_service/debug_service.hpp"
#include "debug_service/json_utils.hpp"
#include "dap/protocol/protocol_events.hpp"
#include "handlers/request_handler.hpp"
#include "llvm/Support/Base64.h"

using namespace dap::protocol;

namespace dap {

// Writes bytes to memory at the provided location.
//
// Clients should only call this request if the corresponding capability
//  supportsWriteMemoryRequest is true.
llvm::Expected<WriteMemoryResponseBody>
WriteMemoryRequestHandler::Run(const WriteMemoryArguments &args) const {
  const lldb::addr_t address = args.memoryReference + args.offset;

  lldb::SBProcess process = dap.target.GetProcess();
  if (!lldb::SBDebugger::StateIsStoppedState(process.GetState()))
    return llvm::make_error<NotStoppedError>();

  if (args.data.empty()) {
    return llvm::make_error<DAPError>(
        "Data cannot be empty value. Provide valid data");
  }

  // The VSCode IDE or other DebugService clients send memory data as a Base64 string.
  // This function decodes it into raw binary before writing it to the target
  // process memory.
  std::vector<char> output;
  auto decode_error = llvm::decodeBase64(args.data, output);

  if (decode_error) {
    return llvm::make_error<DAPError>(
        llvm::toString(std::move(decode_error)).c_str());
  }

  llvm::Expected<core::MemoryWriteResult> result =
      dap.Context().Memory().WriteMemory(address, output, args.allowPartial);
  if (!result)
    return result.takeError();

  WriteMemoryResponseBody response;
  response.bytesWritten = result->bytes_written;

  // Also send invalidated event to signal client that some things
  // (e.g. variables) can be changed.
  dap.Context().Execution().SendInvalidatedEvent({InvalidatedEventBody::eAreaAll});

  return response;
}

} // namespace dap
