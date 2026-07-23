#ifndef TRAILER_DAP_PROTOCOL_SUPPORT_HPP_
#define TRAILER_DAP_PROTOCOL_SUPPORT_HPP_

#include <cstdint>
#include <optional>
#include <string>

#include "lldb/lldb-types.h"
#include "llvm/Support/JSON.h"

// Vendored from LLVM's lldb-dap (JSONUtils.h/.cpp, ProtocolUtils.h/.cpp;
// llvm-project, Apache-2.0 WITH LLVM-exception). Protocol/* calls exactly
// three free functions from those files, none of which touch the SB API -
// they are pulled out here rather than vendoring JSONUtils/ProtocolUtils
// wholesale, since the rest of those files serialize live SBThread/SBFrame/
// SBTarget objects (Handler-layer concerns, not Protocol's).

namespace dap {

// Encodes a memory reference (e.g. "0x1000").
std::string EncodeMemoryReference(lldb::addr_t addr);

// Decodes a memory reference from a "0x..."-prefixed string.
std::optional<lldb::addr_t> DecodeMemoryReference(llvm::StringRef memoryReference);

// Decodes a memory reference from `key` in the JSON object `v`. See
// DecodeMemoryReference(llvm::StringRef) for the string format; `required`
// controls whether a missing key is an error, `allow_empty` whether an
// empty string decodes to LLDB_INVALID_ADDRESS rather than being rejected.
bool DecodeMemoryReference(const llvm::json::Value &v, llvm::StringLiteral key, lldb::addr_t &out,
                            llvm::json::Path path, bool required, bool allow_empty = false);

// Formats a byte count as a human-readable size (e.g. "1.0KB", "2.3MB").
std::string ConvertDebugInfoSizeToString(uint64_t debug_size);

}  // namespace dap

#endif  // TRAILER_DAP_PROTOCOL_SUPPORT_HPP_
