#ifndef TRAILER_TRACE_MODEL_DECODED_INSTRUCTION_HPP_
#define TRAILER_TRACE_MODEL_DECODED_INSTRUCTION_HPP_

#include <cstdint>
#include <optional>
#include <string>

#include "trace_model/instruction_set.hpp"

namespace model {

// A single disassembled instruction: address, encoding, and text, plus
// enough control-flow classification to support later stages (dynamic vs.
// static call-graph comparison, CFI checks) without re-disassembling.
struct DecodedInstruction {
    uint64_t address;
    uint8_t size;  // Encoded length in bytes (2 or 4 for Thumb, 4 for ARM).
    InstructionSet isa;

    std::string bytes;      // Raw encoding, e.g. "04 b0".
    std::string mnemonic;   // e.g. "add".
    std::string operands;   // e.g. "sp, sp, #16".
    std::string text;       // Full assembled line (mnemonic + operands).

    bool is_branch;
    bool is_call;
    bool is_return;
    bool is_indirect;
    bool is_conditional;

    // Resolved target for direct branches only; absent for indirect
    // branches/calls/returns, where the target isn't known until replay.
    std::optional<uint64_t> branch_target;
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_DECODED_INSTRUCTION_HPP_
