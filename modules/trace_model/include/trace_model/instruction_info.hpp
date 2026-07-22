#ifndef TRAILER_TRACE_MODEL_INSTRUCTION_INFO_HPP_
#define TRAILER_TRACE_MODEL_INSTRUCTION_INFO_HPP_

#include "trace_model/decoded_instruction.hpp"
#include "trace_model/source_location.hpp"

namespace model {

// A single instruction's full static info: disassembly plus the source
// location its address resolves to. Precomputed once for an entire
// firmware image by disasm::ProgramDisassembler - the engine's single
// source of truth for everything statically knowable about an
// instruction's address. Later stages only ever look this up (by address),
// never recompute it.
struct InstructionInfo {
    DecodedInstruction insn;
    SourceLocation location;
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_INSTRUCTION_INFO_HPP_
