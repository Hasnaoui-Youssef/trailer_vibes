#ifndef TRAILER_TRACE_SINK_TRACE_RECORD_HPP_
#define TRAILER_TRACE_SINK_TRACE_RECORD_HPP_

#include <cstdint>
#include <optional>

#include <opencsd.h>

namespace trace {

// The subset of OpenCSD generic trace element types the engine retains.
enum class TraceRecordKind : uint8_t {
    kInstructionRange,
    kException,
    kExceptionReturn,
    kTraceOn,
    kNoSync,
};

// A single decoded execution record kept from the OpenCSD generic trace
// element stream. Only instruction-range, exception entry/exit, trace-on,
// and no-sync elements become records; every other element type is dropped
// at the sink.
struct TraceRecord {
    ocsd_trc_index_t index_sop;  // Trace index of the packet that produced this element.
    uint8_t trace_id;            // CoreSight trace ID of the source.
    TraceRecordKind kind;
    ocsd_isa isa;

    // Instruction range: [start_addr, end_addr) executed, end_addr exclusive.
    // Exception / exception return: start_addr is the exception target /
    // return address; end_addr is the preferred return address, valid only
    // when has_end_addr is set.
    ocsd_vaddr_t start_addr;
    ocsd_vaddr_t end_addr;
    bool has_end_addr;

    // Instruction range only.
    uint32_t num_instr;
    ocsd_instr_type last_instr_type;
    ocsd_instr_subtype last_instr_subtype;
    bool last_instr_executed;

    // Exception / exception return only.
    uint32_t exception_number;

    // Trace-on only: why tracing (re)started (normal start, overflow
    // recovery, debug-exit restart). Marks a discontinuity boundary.
    trace_on_reason_t trace_on_reason;

    // No-sync only: why the decoder is waiting for resync (init, overflow,
    // bad packet/image, discard, end-of-trace). Marks the start of a gap in
    // the trace stream.
    unsync_info_t no_sync_reason;

    // Cycle count / global timestamp riding on this record, when the source
    // packet carried one (OpenCSD's has_cc/has_ts - these attach to any
    // element kind, most often an instruction range, not just a dedicated
    // cycle-count/timestamp element).
    std::optional<uint32_t> cycle_count;
    std::optional<uint64_t> timestamp;

    // ETMv4: cycles since the last Cycle Count element, embedded in a
    // Timestamp element. Not part of the cumulative cycle count - keep
    // separate from cycle_count above.
    std::optional<uint32_t> timestamp_cycle_count;
};

}  // namespace trace

#endif  // TRAILER_TRACE_SINK_TRACE_RECORD_HPP_
