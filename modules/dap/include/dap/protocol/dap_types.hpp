//===-- ProtocolTypes.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains private DAP types used in the protocol.
//
// Each struct has a toJSON and fromJSON function, that converts between
// the struct and a JSON representation. (See JSON.h)
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_DAP_PROTOCOL_DAP_TYPES_HPP_
#define TRAILER_DAP_PROTOCOL_DAP_TYPES_HPP_

#include "dap/protocol/dap_defines.hpp"
#include "llvm/Support/JSON.h"
#include <optional>
#include <string>

namespace dap::protocol {

/// Mirrors lldb::SymbolType (lldb/lldb-enumerations.h) value-for-value, so
/// providers/lldb's conversion at the LLDB boundary is a plain static_cast.
/// dap_protocol never includes LLDB - see dap_defines.hpp.
enum class SymbolType {
  eSymbolTypeInvalid = 0,
  eSymbolTypeAbsolute,
  eSymbolTypeCode,
  eSymbolTypeResolver,
  eSymbolTypeData,
  eSymbolTypeTrampoline,
  eSymbolTypeRuntime,
  eSymbolTypeException,
  eSymbolTypeSourceFile,
  eSymbolTypeHeaderFile,
  eSymbolTypeObjectFile,
  eSymbolTypeCommonBlock,
  eSymbolTypeBlock,
  eSymbolTypeLocal,
  eSymbolTypeParam,
  eSymbolTypeVariable,
  eSymbolTypeVariableType,
  eSymbolTypeLineEntry,
  eSymbolTypeLineHeader,
  eSymbolTypeScopeBegin,
  eSymbolTypeScopeEnd,
  eSymbolTypeAdditional,
  eSymbolTypeCompiler,
  eSymbolTypeInstrumentation,
  eSymbolTypeUndefined,
  eSymbolTypeObjCClass,
  eSymbolTypeObjCMetaClass,
  eSymbolTypeObjCIVar,
  eSymbolTypeReExported,
};

/// Wire-format name for `type` (e.g. "Code", "Data") - matches what
/// lldb::SBSymbol::GetTypeAsString produced, verified against a live
/// liblldb before porting.
llvm::StringRef SymbolTypeToString(SymbolType type);

/// Reverse of SymbolTypeToString; returns eSymbolTypeInvalid for an
/// unrecognized string.
SymbolType SymbolTypeFromString(llvm::StringRef str);

/// Data used to help lldb-dap resolve breakpoints persistently across different
/// sessions. This information is especially useful for assembly breakpoints,
/// because `sourceReference` can change across sessions. For regular source
/// breakpoints the path and line are the same For each session.
struct PersistenceData {
  /// The source module path.
  std::string module_path;

  /// The symbol name of the Source.
  std::string symbol_name;
};
bool fromJSON(const llvm::json::Value &, PersistenceData &, llvm::json::Path);
llvm::json::Value toJSON(const PersistenceData &);

/// Custom source data used by lldb-dap.
/// This data should help lldb-dap identify sources correctly across different
/// sessions.
struct SourceLLDBData {
  /// Data that helps lldb resolve this source persistently across different
  /// sessions.
  std::optional<PersistenceData> persistenceData;
};
bool fromJSON(const llvm::json::Value &, SourceLLDBData &, llvm::json::Path);
llvm::json::Value toJSON(const SourceLLDBData &);

struct Symbol {
  /// The symbol id, usually the original symbol table index.
  uint32_t id = 0;

  /// True if this symbol is debug information in a symbol.
  bool isDebug = false;

  /// True if this symbol is not actually in the symbol table, but synthesized
  /// from other info in the object file.
  bool isSynthetic = false;

  /// True if this symbol is globally visible.
  bool isExternal = false;

  /// The symbol type.
  SymbolType type = SymbolType::eSymbolTypeInvalid;

  /// The symbol file address.
  addr_t fileAddress = LLDB_INVALID_ADDRESS;

  /// The symbol load address.
  std::optional<addr_t> loadAddress;

  /// The symbol size.
  addr_t size = 0;

  /// The symbol name.
  std::string name;
};
bool fromJSON(const llvm::json::Value &, Symbol &, llvm::json::Path);
llvm::json::Value toJSON(const Symbol &);

} // namespace dap::protocol

#endif  // TRAILER_DAP_PROTOCOL_DAP_TYPES_HPP_
