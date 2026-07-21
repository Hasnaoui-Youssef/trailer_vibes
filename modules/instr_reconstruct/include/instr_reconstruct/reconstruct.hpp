#ifndef TRAILER_INSTR_RECONSTRUCT_RECONSTRUCT_HPP_
#define TRAILER_INSTR_RECONSTRUCT_RECONSTRUCT_HPP_

#include <vector>

#include "disassembler/program_disassembler.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_sink/trace_record.hpp"

namespace reconstruct {

// Bridges the OpenCSD side of the pipeline (TraceRecord instruction ranges)
// to the LLVM side (ProgramDisassembler): walks `records` in trace order,
// disassembling each kInstructionRange into individual instructions.
//
// Every other TraceRecordKind (exception, exception return, trace-on,
// no-sync) is a non-instruction event and is intentionally left out of the
// returned stream, rather than duplicated into it; ReconstructedInstruction
// ::trace_index correlates each instruction back to its originating
// TraceRecord::index_sop, so a caller that also holds `records` (it always
// does - `records` is an input, not consumed) can still recover the full
// interleaved context (e.g. "this line block was interrupted here") by
// walking `records` directly alongside the returned instructions.
//
// This is the one function in the engine allowed to see both an OpenCSD
// type (trace::TraceRecord) and the disassembler's LLVM-free interface. It
// never includes an LLVM header itself, so unlike disassembler it is
// compiled with the project's default RTTI setting - safe to link
// alongside OpenCSD, which requires RTTI.
std::vector<model::ReconstructedInstruction> Reconstruct(const std::vector<trace::TraceRecord> &records,
                                                          const disasm::ProgramDisassembler &disassembler);

}  // namespace reconstruct

#endif  // TRAILER_INSTR_RECONSTRUCT_RECONSTRUCT_HPP_
