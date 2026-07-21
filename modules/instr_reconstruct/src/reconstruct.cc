#include "instr_reconstruct/reconstruct.hpp"

#include <cstddef>
#include <iostream>
#include <utility>

namespace reconstruct {

namespace {

model::InstructionSet ToInstructionSet(ocsd_isa isa) {
    switch (isa) {
        case ocsd_isa_arm:
            return model::InstructionSet::kArm;
        case ocsd_isa_thumb2:
            return model::InstructionSet::kThumb;
        default:
            return model::InstructionSet::kUnknown;
    }
}

}  // namespace

std::vector<model::ReconstructedInstruction> Reconstruct(const std::vector<trace::TraceRecord> &records,
                                                          const disasm::ProgramDisassembler &disassembler) {
    std::vector<model::ReconstructedInstruction> result;

    for (const trace::TraceRecord &record : records) {
        if (record.kind != trace::TraceRecordKind::kInstructionRange) {
            continue;
        }

        const model::InstructionSet isa = ToInstructionSet(record.isa);
        if (isa == model::InstructionSet::kUnknown) {
            std::cerr << "instr_reconstruct: unsupported ISA for range at trace index " << record.index_sop << "\n";
            continue;
        }

        std::vector<model::DecodedInstruction> decoded =
            disassembler.DisassembleRange(record.start_addr, record.end_addr, isa);
        if (decoded.empty()) {
            continue;
        }

        // A not-taken conditional at the tail of the range means the last
        // decoded instruction here did not actually execute (see
        // TraceRecord::last_instr_executed).
        const size_t last_index = decoded.size() - 1;

        for (size_t i = 0; i < decoded.size(); ++i) {
            model::ReconstructedInstruction reconstructed;
            reconstructed.insn = std::move(decoded[i]);
            reconstructed.trace_index = record.index_sop;
            reconstructed.trace_id = record.trace_id;
            reconstructed.executed = !(i == last_index && !record.last_instr_executed);
            result.push_back(std::move(reconstructed));
        }
    }

    return result;
}

}  // namespace reconstruct
