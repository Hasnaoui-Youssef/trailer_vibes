//===-- dap_defines.hpp ----------------------------------------------------===//
//
// Plain, fixed-width stand-ins for the lldb:: ID typedefs (lldb/lldb-types.h)
// and their LLDB_INVALID_* sentinels (lldb/lldb-defines.h) that the wire
// protocol structs use. dap_protocol must never depend on LLDB (see
// CLAUDE.md's "DAP should not know about the SB API at all" rule) -
// core/providers convert to/from the real lldb:: types at the LLDB
// boundary. Values and macro names match lldb's exactly (verified against
// lldb/lldb-defines.h), so a translation unit that also includes the real
// lldb-defines.h sees an identical (not an error) redefinition.
//
//===----------------------------------------------------------------------===//

#ifndef TRAILER_DAP_PROTOCOL_DAP_DEFINES_HPP_
#define TRAILER_DAP_PROTOCOL_DAP_DEFINES_HPP_

#include <cstdint>

namespace dap::protocol {

using addr_t = uint64_t;
using tid_t = uint64_t;
using pid_t = uint64_t;
using user_id_t = uint64_t;

}  // namespace dap::protocol

#ifndef LLDB_INVALID_ADDRESS
#define LLDB_INVALID_ADDRESS UINT64_MAX
#endif
#ifndef LLDB_INVALID_THREAD_ID
#define LLDB_INVALID_THREAD_ID 0
#endif
#ifndef LLDB_INVALID_PROCESS_ID
#define LLDB_INVALID_PROCESS_ID 0
#endif
#ifndef LLDB_INVALID_UID
#define LLDB_INVALID_UID UINT64_MAX
#endif
#ifndef LLDB_INVALID_LINE_NUMBER
#define LLDB_INVALID_LINE_NUMBER UINT32_MAX
#endif
#ifndef LLDB_INVALID_COLUMN_NUMBER
#define LLDB_INVALID_COLUMN_NUMBER 0
#endif

#endif  // TRAILER_DAP_PROTOCOL_DAP_DEFINES_HPP_
