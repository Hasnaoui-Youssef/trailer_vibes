// Deterministic unit test for ProgramDisassembler: disassembles a tiny,
// pre-built Thumb-2 firmware image (fixture.elf, see fixture.s) and checks
// PT_LOAD segment extraction plus per-instruction decoding, including
// control-flow classification (direct call + branch-target resolution,
// return detection).

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "disassembler/program_disassembler.hpp"
#include "trace_model/decoded_instruction.hpp"
#include "trace_model/instruction_set.hpp"
#include "trace_model/load_segment.hpp"

#ifndef TRAILER_DISASSEMBLER_FIXTURE_PATH
#error "TRAILER_DISASSEMBLER_FIXTURE_PATH must be defined by the build"
#endif

namespace {

constexpr uint64_t kBase = 0x08000000;

int Fail(const std::string &message) {
    std::cerr << "program_disassembler_test: " << message << "\n";
    return EXIT_FAILURE;
}

}  // namespace

int main() {
    const disasm::CreateResult create_result = disasm::ProgramDisassembler::Create(TRAILER_DISASSEMBLER_FIXTURE_PATH);
    if (!create_result.Ok()) {
        return Fail("Create failed: " + create_result.error);
    }
    const disasm::ProgramDisassembler &disassembler = *create_result.disassembler;

    // --- PT_LOAD segment extraction ---
    const std::vector<model::LoadSegment> &segments = disassembler.load_segments();
    if (segments.size() != 1) {
        return Fail("expected exactly one PT_LOAD segment, got " + std::to_string(segments.size()));
    }
    const model::LoadSegment &segment = segments.front();
    if (segment.vaddr != kBase || segment.file_offset != 0x1000 || segment.size != 0xc) {
        return Fail("unexpected load segment bounds");
    }

    // --- Disassembly + control-flow classification over [base, base+8):
    //     add sp, sp, #16 ; bl target ; bx lr
    const std::vector<model::DecodedInstruction> instrs =
        disassembler.DisassembleRange(kBase, kBase + 0x8, model::InstructionSet::kThumb);

    if (instrs.size() != 3) {
        return Fail("expected 3 instructions in [base, base+8), got " + std::to_string(instrs.size()));
    }

    const model::DecodedInstruction &add_instr = instrs[0];
    if (add_instr.address != kBase || add_instr.size != 2 || add_instr.bytes != "04 b0") {
        return Fail("unexpected 'add' encoding: " + add_instr.bytes);
    }
    if (add_instr.mnemonic != "add") {
        return Fail("unexpected 'add' mnemonic: " + add_instr.mnemonic);
    }
    if (add_instr.operands.find("16") == std::string::npos) {
        return Fail("unexpected 'add' operands: " + add_instr.operands);
    }
    if (add_instr.is_branch || add_instr.is_call || add_instr.is_return || add_instr.is_indirect) {
        return Fail("'add' misclassified as control flow");
    }

    const model::DecodedInstruction &bl_instr = instrs[1];
    if (bl_instr.address != kBase + 2 || bl_instr.size != 4 || bl_instr.bytes != "00 f0 02 f8") {
        return Fail("unexpected 'bl' encoding: " + bl_instr.bytes);
    }
    if (bl_instr.mnemonic != "bl") {
        return Fail("unexpected 'bl' mnemonic: " + bl_instr.mnemonic);
    }
    if (!bl_instr.is_call || !bl_instr.is_branch || bl_instr.is_indirect || bl_instr.is_return) {
        return Fail("'bl' not classified as a direct call");
    }
    // target label is at kBase + 0xa (see fixture.s).
    if (!bl_instr.branch_target.has_value() || *bl_instr.branch_target != kBase + 0xa) {
        return Fail("'bl' branch_target not resolved to the 'target' label");
    }

    const model::DecodedInstruction &bx_instr = instrs[2];
    if (bx_instr.address != kBase + 6 || bx_instr.size != 2 || bx_instr.bytes != "70 47") {
        return Fail("unexpected 'bx' encoding: " + bx_instr.bytes);
    }
    if (bx_instr.mnemonic != "bx") {
        return Fail("unexpected 'bx' mnemonic: " + bx_instr.mnemonic);
    }
    // Raw disassembly only ever sees the generic indirect-branch encoding
    // (LLVM's tablegen "isReturn" flag is reserved for a CodeGen-only
    // pseudo the disassembler never produces) - ProgramDisassembler layers
    // the conventional "bx lr" == return heuristic on top; see
    // LooksLikeReturn in program_disassembler.cc.
    if (!bx_instr.is_indirect || !bx_instr.is_branch || bx_instr.is_call) {
        return Fail("'bx lr' not classified as an indirect branch");
    }
    if (!bx_instr.is_return) {
        return Fail("'bx lr' not classified as a return");
    }

    std::cerr << "program_disassembler_test: OK\n";
    return EXIT_SUCCESS;
}
