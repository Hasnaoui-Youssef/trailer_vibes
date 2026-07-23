//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "debug_service/debug_service.hpp"
#include "debug_service/dap_error.hpp"
#include "dap/protocol/dap_types.hpp"
#include "debug_service/request_handler.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBModuleSpec.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>

using namespace dap::protocol;
namespace dap::debug_service {

namespace {
// Decodes UUID bytes from a hex string (optionally '-'-separated), stopping
// at the first byte pair that can't be decoded and returning the leftover
// suffix - same contract as lldb_private::UUID::DecodeUUIDBytesFromString,
// reimplemented locally since lldb_private is off-limits here (see
// project-dap-layer-fork-strategy memory).
llvm::StringRef DecodeUUIDBytesFromString(llvm::StringRef str, llvm::SmallVectorImpl<uint8_t> &uuid_bytes) {
  auto hex_value = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  while (!str.empty()) {
    if (str.front() == '-') {
      str = str.drop_front();
      continue;
    }
    if (str.size() < 2)
      break;
    const int hi = hex_value(str[0]);
    const int lo = hex_value(str[1]);
    if (hi < 0 || lo < 0)
      break;
    uuid_bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    str = str.drop_front(2);
  }
  return str;
}
}  // namespace

llvm::Expected<ModuleSymbolsResponseBody>
ModuleSymbolsRequestHandler::Run(const ModuleSymbolsArguments &args) const {
  ModuleSymbolsResponseBody response;

  lldb::SBModuleSpec module_spec;
  if (!args.moduleId.empty()) {
    llvm::SmallVector<uint8_t, 20> uuid_bytes;
    if (!DecodeUUIDBytesFromString(args.moduleId, uuid_bytes).empty())
      return llvm::make_error<DAPError>("invalid module ID");

    module_spec.SetUUIDBytes(uuid_bytes.data(), uuid_bytes.size());
  }

  if (!args.moduleName.empty()) {
    lldb::SBFileSpec file_spec;
    file_spec.SetFilename(args.moduleName.c_str());
    module_spec.SetFileSpec(file_spec);
  }

  // Empty request, return empty response.
  if (!module_spec.IsValid())
    return response;

  std::vector<Symbol> &symbols = response.symbols;
  lldb::SBModule module = dap.target.FindModule(module_spec);
  if (!module.IsValid())
    return llvm::make_error<DAPError>("module not found");

  const size_t num_symbols = module.GetNumSymbols();
  const size_t start_index = args.startIndex.value_or(0);
  const size_t end_index =
      std::min(start_index + args.count.value_or(num_symbols), num_symbols);
  for (size_t i = start_index; i < end_index; ++i) {
    lldb::SBSymbol symbol = module.GetSymbolAtIndex(i);
    if (!symbol.IsValid())
      continue;

    Symbol dap_symbol;
    dap_symbol.id = symbol.GetID();
    dap_symbol.type = symbol.GetType();
    dap_symbol.isDebug = symbol.IsDebug();
    dap_symbol.isSynthetic = symbol.IsSynthetic();
    dap_symbol.isExternal = symbol.IsExternal();

    lldb::SBAddress start_address = symbol.GetStartAddress();
    if (start_address.IsValid()) {
      lldb::addr_t file_address = start_address.GetFileAddress();
      if (file_address != LLDB_INVALID_ADDRESS)
        dap_symbol.fileAddress = file_address;

      lldb::addr_t load_address = start_address.GetLoadAddress(dap.target);
      if (load_address != LLDB_INVALID_ADDRESS)
        dap_symbol.loadAddress = load_address;
    }

    dap_symbol.size = symbol.GetSize();
    if (const char *symbol_name = symbol.GetName())
      dap_symbol.name = symbol_name;
    symbols.push_back(std::move(dap_symbol));
  }

  return response;
}

} // namespace dap::debug_service
