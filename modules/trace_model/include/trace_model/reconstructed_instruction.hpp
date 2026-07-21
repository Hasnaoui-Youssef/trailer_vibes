#ifndef TRAILER_TRACE_MODEL_RECONSTRUCTED_INSTRUCTION_HPP_
#define TRAILER_TRACE_MODEL_RECONSTRUCTED_INSTRUCTION_HPP_

#include <cstdint>

#include "trace_model/decoded_instruction.hpp"

namespace model {

// A DecodedInstruction plus its provenance in the raw trace: which
// TraceRecord produced it and whether it actually executed. Produced by
// instr_reconstruct by walking TraceRecord instruction ranges through the
// disassembler in trace order.
struct ReconstructedInstruction {
    DecodedInstruction insn;

    uint64_t trace_index;  // Originating TraceRecord::index_sop.
    uint8_t trace_id;      // Originating TraceRecord::trace_id.

    // False only for a range's last instruction when the originating
    // TraceRecord::last_instr_executed was false (e.g. a not-taken
    // conditional at the end of the range). True otherwise.
    bool executed;
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_RECONSTRUCTED_INSTRUCTION_HPP_
