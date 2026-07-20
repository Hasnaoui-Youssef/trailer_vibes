#ifndef TRAILER_TRACE_SINK_TRACE_RECORD_HPP_
#define TRAILER_TRACE_SINK_TRACE_RECORD_HPP_

#include <cstdint>

#include <opencsd.h>

namespace trace {

// The subset of OpenCSD generic trace element types the engine retains.
enum class TraceRecordKind : uint8_t {
    kInstructionRange,
    kException,
    kExceptionReturn,
};

// A single decoded execution record kept from the OpenCSD generic trace
// element stream. Only instruction-range and exception entry/exit elements
// become records; every other element type is dropped at the sink.
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
};

}  // namespace trace

#endif  // TRAILER_TRACE_SINK_TRACE_RECORD_HPP_
