#include "dap/protocol_support.hpp"

#include <iomanip>
#include <sstream>

#include "llvm/ADT/StringExtras.h"

// Vendored from LLVM's lldb-dap (JSONUtils.cpp::EncodeMemoryReference/
// DecodeMemoryReference, ProtocolUtils.cpp::ConvertDebugInfoSizeToString;
// llvm-project, Apache-2.0 WITH LLVM-exception), verbatim apart from the
// namespace.

namespace dap {

std::string EncodeMemoryReference(protocol::addr_t addr) { return "0x" + llvm::utohexstr(addr); }

std::optional<protocol::addr_t> DecodeMemoryReference(llvm::StringRef memoryReference) {
    if (!memoryReference.starts_with("0x")) {
        return std::nullopt;
    }

    protocol::addr_t addr;
    if (memoryReference.consumeInteger(0, addr)) {
        return std::nullopt;
    }

    return addr;
}

bool DecodeMemoryReference(const llvm::json::Value &v, llvm::StringLiteral key, protocol::addr_t &out,
                            llvm::json::Path path, bool required, bool allow_empty) {
    const llvm::json::Object *v_obj = v.getAsObject();
    if (!v_obj) {
        path.report("expected object");
        return false;
    }

    const llvm::json::Value *mem_ref_value = v_obj->get(key);
    if (!mem_ref_value) {
        if (!required) {
            return true;
        }
        path.field(key).report("missing value");
        return false;
    }

    const std::optional<llvm::StringRef> mem_ref_str = mem_ref_value->getAsString();
    if (!mem_ref_str) {
        path.field(key).report("expected string");
        return false;
    }

    if (allow_empty && mem_ref_str->empty()) {
        out = LLDB_INVALID_ADDRESS;
        return true;
    }

    const std::optional<protocol::addr_t> addr_opt = DecodeMemoryReference(*mem_ref_str);
    if (!addr_opt) {
        path.field(key).report("malformed memory reference");
        return false;
    }

    out = *addr_opt;
    return true;
}

std::string ConvertDebugInfoSizeToString(uint64_t debug_size) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    if (debug_size < 1024) {
        oss << debug_size << "B";
    } else if (debug_size < static_cast<uint64_t>(1024 * 1024)) {
        oss << (double(debug_size) / 1024.0) << "KB";
    } else if (debug_size < static_cast<uint64_t>(1024) * 1024 * 1024) {
        oss << (double(debug_size) / (1024.0 * 1024.0)) << "MB";
    } else {
        oss << (double(debug_size) / (1024.0 * 1024.0 * 1024.0)) << "GB";
    }
    return oss.str();
}

}  // namespace dap
