#ifndef TRAILER_TRACE_MODEL_INSTRUCTION_SET_HPP_
#define TRAILER_TRACE_MODEL_INSTRUCTION_SET_HPP_

#include <cstdint>

namespace model {

// Instruction set an address range was executed as. Deliberately independent
// of both OpenCSD's ocsd_isa and LLVM's target triples/subtargets: this is
// the neutral vocabulary that lets code on either side of the RTTI boundary
// (see disassembler / trace_transform) share a type without either side
// including the other's headers.
enum class InstructionSet : uint8_t {
    kArm,      // AArch32 ARM (A32), 4-byte instructions.
    kThumb,    // AArch32 Thumb/Thumb-2 (T32), 2- or 4-byte instructions.
    kUnknown,  // Not representable by the current disassembler backend.
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_INSTRUCTION_SET_HPP_
