#ifndef TRAILER_TRACE_TRANSFORM_RECONSTRUCTED_INSTRUCTION_TRANSFORM_HPP_
#define TRAILER_TRACE_TRANSFORM_RECONSTRUCTED_INSTRUCTION_TRANSFORM_HPP_

#include <cstdint>
#include <iostream>
#include <span>
#include <utility>
#include <vector>

#include <opencsd.h>

#include "trace_model/instruction_info.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_sink/trace_record.hpp"
#include "trace_transform/transform.hpp"

namespace xform {

// The engine's TraceRecord -> instruction-level projection: expands each
// kInstructionRange record into its individual instructions, resolved via
// `resolve` (any callable `uint64_t address -> const model::InstructionInfo*`,
// e.g. a lambda wrapping disasm::ProgramDisassembler::InstructionInfoAt).
// Everything instruction-level - a flat printed listing, function/line
// grouping (see function_block_transform.hpp) - is built from this
// specialization's output, not by separately re-walking records.
//
// Every other TraceRecordKind (exception, exception return, trace-on,
// no-sync) is a non-instruction event and is intentionally left out of the
// returned stream rather than duplicated into it; ReconstructedInstruction
// ::trace_index correlates each instruction back to its originating
// TraceRecord::index_sop, so a caller that also holds `records` can still
// recover the full interleaved context by walking `records` directly
// alongside the returned instructions.
template <>
struct TraceTransform<model::ReconstructedInstruction> {
    template <typename Resolve>
    static std::vector<model::ReconstructedInstruction> Apply(std::span<const trace::TraceRecord> records,
                                                                Resolve resolve) {
        std::vector<model::ReconstructedInstruction> result;

        for (const trace::TraceRecord &record : records) {
            if (record.kind != trace::TraceRecordKind::kInstructionRange) {
                continue;
            }

            uint64_t cur = record.start_addr;
            while (cur < record.end_addr) {
                const model::InstructionInfo *info = resolve(cur);
                if (info == nullptr) {
                    // A gap in the precomputed table for an address the
                    // trace says was executed is a real precompute/decode
                    // problem, not something to guess through.
                    std::cerr << "trace_transform: no precomputed instruction at 0x" << std::hex << cur << std::dec
                               << " (trace index " << record.index_sop << "); stopping this range early\n";
                    break;
                }

                // Cross-check: precomputation determines ISA statically
                // (ARM mapping symbols), while the trace's own per-range
                // ocsd_isa is an independent, dynamically-observed signal.
                // They should always agree for this engine's actual target
                // (Cortex-M, Thumb-only) - a disagreement is cheap to catch
                // and worth surfacing rather than silently trusting one
                // side over the other.
                if (record.isa == ocsd_isa_thumb2 && info->insn.isa != model::InstructionSet::kThumb) {
                    std::cerr << "trace_transform: ISA mismatch at 0x" << std::hex << cur << std::dec
                               << ": trace says Thumb, precomputed instruction disagrees\n";
                } else if (record.isa == ocsd_isa_arm && info->insn.isa != model::InstructionSet::kArm) {
                    std::cerr << "trace_transform: ISA mismatch at 0x" << std::hex << cur << std::dec
                               << ": trace says ARM, precomputed instruction disagrees\n";
                }

                const uint64_t next = cur + info->insn.size;
                const bool is_last_in_range = next >= record.end_addr;

                model::ReconstructedInstruction reconstructed;
                reconstructed.insn = info->insn;  // Copy: info aliases the shared precomputed table, not moved-from.
                reconstructed.trace_index = record.index_sop;
                reconstructed.trace_id = record.trace_id;
                reconstructed.executed = !(is_last_in_range && !record.last_instr_executed);
                result.push_back(std::move(reconstructed));

                cur = next;
            }
        }

        return result;
    }
};

}  // namespace xform

#endif  // TRAILER_TRACE_TRANSFORM_RECONSTRUCTED_INSTRUCTION_TRANSFORM_HPP_
