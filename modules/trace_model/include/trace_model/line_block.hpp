#ifndef TRAILER_TRACE_MODEL_LINE_BLOCK_HPP_
#define TRAILER_TRACE_MODEL_LINE_BLOCK_HPP_

#include <cstdint>

#include "trace_model/source_location.hpp"

namespace model {

// A maximal, chronological run of consecutively-executed instructions
// resolving to the same SourceLocation (same full inline chain, not just
// the same innermost file:line - see trace_transform's FunctionBlock
// grouping specialization). One loop executed N times produces N separate
// LineBlocks, not one merged block: this preserves per-invocation
// information (timing, call depth) that later analysis phases need, and a
// per-line/per-function aggregate view is a cheap projection over this
// sequence if that's ever wanted instead. Locations resolving to an
// assembly source file are a deliberate exception: they never merge with
// an adjacent block regardless of location equality, since an assembly
// line is essentially always exactly one instruction, making the merge
// uninformative there.
struct LineBlock {
    SourceLocation location;

    uint64_t start_addr;  // First instruction's address in this block.
    uint64_t end_addr;    // One past the last instruction's address (exclusive).
    uint32_t instr_count;

    // Provenance of the block's first instruction, for tracing back to the
    // raw trace (see ReconstructedInstruction::trace_index/trace_id).
    uint64_t trace_index;
    uint8_t trace_id;
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_LINE_BLOCK_HPP_
