// Deterministic unit test for ProgramDisassembler, the engine's single
// source of truth for instruction-level info: disassembly (bytes/mnemonic/
// operands/control-flow classification) AND source correlation (DWARF
// file/line/inline-frame chain), both precomputed once at Create() time and
// queried only via InstructionInfoAt thereafter.
//
// Three fixtures, three concerns:
//  - fixture.s/fixture.elf (no debug info): PT_LOAD extraction + disassembly
//    + control-flow classification (direct call, return detection).
//  - dwarf_fixture.c/dwarf_fixture.elf (real DWARF, forced inlining):
//    source-location resolution, including full inline-frame chains.
//  - the real STM32H7 test vector (test_resources/firmware.elf): a
//    concrete regression check that the mapping-symbol-guided precompute
//    pass actually skips data-in-code (a real Thumb literal pool embedded
//    in .text) rather than decoding it as bogus instructions - confirmed
//    present via `readelf -sW firmware.elf` before writing this test.

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <string>
#include <vector>

#include "disassembler/program_disassembler.hpp"
#include "trace_model/instruction_info.hpp"
#include "trace_model/load_segment.hpp"

#ifndef TRAILER_DISASSEMBLER_FIXTURE_PATH
#error "TRAILER_DISASSEMBLER_FIXTURE_PATH must be defined by the build"
#endif
#ifndef TRAILER_DWARF_FIXTURE_PATH
#error "TRAILER_DWARF_FIXTURE_PATH must be defined by the build"
#endif
#ifndef TRAILER_FIRMWARE_PATH
#error "TRAILER_FIRMWARE_PATH must be defined by the build"
#endif

namespace {

constexpr uint64_t kBase = 0x08000000;

int Fail(const std::string &message) {
    std::cerr << "program_disassembler_test: " << message << "\n";
    return EXIT_FAILURE;
}

bool CheckFrame(const model::InlineFrame &frame, const std::string &function, uint32_t line, const std::string &what) {
    // DWARF embeds the compilation-unit name from when dwarf_fixture.elf
    // was originally compiled ("fixture.c") - renaming the file on disk
    // (this repo's dwarf_fixture.c/.elf) doesn't change what's already
    // baked into the debug info without recompiling.
    if (frame.function != function || frame.file != "fixture.c" || frame.line != line) {
        std::cerr << "program_disassembler_test: " << what << ": got function='" << frame.function << "' file='"
                   << frame.file << "' line=" << frame.line << ", expected function='" << function
                   << "' file='fixture.c' line=" << line << "\n";
        return false;
    }
    return true;
}

int CheckPlainDisassembly() {
    std::expected<disasm::ProgramDisassembler, std::string> create_result =
        disasm::ProgramDisassembler::Create(TRAILER_DISASSEMBLER_FIXTURE_PATH);
    if (!create_result) {
        return Fail("fixture.elf Create failed: " + create_result.error());
    }
    const disasm::ProgramDisassembler &disassembler = *create_result;

    // --- PT_LOAD segment extraction ---
    const std::vector<model::LoadSegment> &segments = disassembler.load_segments();
    if (segments.size() != 1) {
        return Fail("expected exactly one PT_LOAD segment, got " + std::to_string(segments.size()));
    }
    const model::LoadSegment &segment = segments.front();
    if (segment.vaddr != kBase || segment.file_offset != 0x1000 || segment.size != 0xc) {
        return Fail("unexpected load segment bounds");
    }

    // --- add sp, sp, #16 @ base: plain instruction, no debug info ---
    const model::InstructionInfo *add_info = disassembler.InstructionInfoAt(kBase);
    if (add_info == nullptr) {
        return Fail("expected an instruction at fixture base address");
    }
    if (add_info->insn.address != kBase || add_info->insn.size != 2 || add_info->insn.bytes != "04 b0") {
        return Fail("unexpected 'add' encoding: " + add_info->insn.bytes);
    }
    if (add_info->insn.mnemonic != "add") {
        return Fail("unexpected 'add' mnemonic: " + add_info->insn.mnemonic);
    }
    if (add_info->insn.operands.find("16") == std::string::npos) {
        return Fail("unexpected 'add' operands: " + add_info->insn.operands);
    }
    if (add_info->insn.is_branch || add_info->insn.is_call || add_info->insn.is_return || add_info->insn.is_indirect) {
        return Fail("'add' misclassified as control flow");
    }
    if (!add_info->location.frames.empty()) {
        return Fail("fixture.elf has no debug info; expected an unresolved location for 'add'");
    }

    // --- bl target @ base+2: direct call, resolved branch target ---
    const model::InstructionInfo *bl_info = disassembler.InstructionInfoAt(kBase + 2);
    if (bl_info == nullptr) {
        return Fail("expected an instruction at base+2");
    }
    if (bl_info->insn.size != 4 || bl_info->insn.bytes != "00 f0 02 f8") {
        return Fail("unexpected 'bl' encoding: " + bl_info->insn.bytes);
    }
    if (bl_info->insn.mnemonic != "bl") {
        return Fail("unexpected 'bl' mnemonic: " + bl_info->insn.mnemonic);
    }
    if (!bl_info->insn.is_call || !bl_info->insn.is_branch || bl_info->insn.is_indirect || bl_info->insn.is_return) {
        return Fail("'bl' not classified as a direct call");
    }
    // target label is at kBase + 0xa (see fixture.s).
    if (!bl_info->insn.branch_target.has_value() || *bl_info->insn.branch_target != kBase + 0xa) {
        return Fail("'bl' branch_target not resolved to the 'target' label");
    }

    // --- bx lr @ base+6: indirect branch, heuristically a return ---
    const model::InstructionInfo *bx_info = disassembler.InstructionInfoAt(kBase + 6);
    if (bx_info == nullptr) {
        return Fail("expected an instruction at base+6");
    }
    if (bx_info->insn.size != 2 || bx_info->insn.bytes != "70 47") {
        return Fail("unexpected 'bx' encoding: " + bx_info->insn.bytes);
    }
    if (bx_info->insn.mnemonic != "bx") {
        return Fail("unexpected 'bx' mnemonic: " + bx_info->insn.mnemonic);
    }
    // Raw disassembly only ever sees the generic indirect-branch encoding
    // (LLVM's tablegen "isReturn" flag is reserved for a CodeGen-only
    // pseudo the disassembler never produces) - ProgramDisassembler layers
    // the conventional "bx lr" == return heuristic on top.
    if (!bx_info->insn.is_indirect || !bx_info->insn.is_branch || bx_info->insn.is_call) {
        return Fail("'bx lr' not classified as an indirect branch");
    }
    if (!bx_info->insn.is_return) {
        return Fail("'bx lr' not classified as a return");
    }

    // --- a mid-instruction / uncovered address must miss cleanly ---
    if (disassembler.InstructionInfoAt(kBase + 1) != nullptr) {
        return Fail("expected no instruction at a mid-instruction offset");
    }

    return EXIT_SUCCESS;
}

int CheckSourceCorrelation() {
    std::expected<disasm::ProgramDisassembler, std::string> create_result =
        disasm::ProgramDisassembler::Create(TRAILER_DWARF_FIXTURE_PATH);
    if (!create_result) {
        return Fail("dwarf_fixture.elf Create failed: " + create_result.error());
    }
    const disasm::ProgramDisassembler &disassembler = *create_result;

    // --- add_one() inlined at 0x08000000, called from compute() at line 10 ---
    const model::InstructionInfo *add_one_info = disassembler.InstructionInfoAt(kBase);
    if (add_one_info == nullptr) {
        return Fail("expected an instruction at add_one's address");
    }
    if (add_one_info->location.frames.size() != 2) {
        return Fail("expected 2 frames resolving add_one's address, got " +
                    std::to_string(add_one_info->location.frames.size()));
    }
    if (!CheckFrame(add_one_info->location.frames[0], "add_one", 2, "add_one innermost frame") ||
        !CheckFrame(add_one_info->location.frames[1], "compute", 10, "add_one outer frame")) {
        return EXIT_FAILURE;
    }

    // --- double_it() inlined at 0x08000002, called from compute() at line 11 ---
    const model::InstructionInfo *double_it_info = disassembler.InstructionInfoAt(kBase + 2);
    if (double_it_info == nullptr) {
        return Fail("expected an instruction at double_it's address");
    }
    if (double_it_info->location.frames.size() != 2) {
        return Fail("expected 2 frames resolving double_it's address, got " +
                    std::to_string(double_it_info->location.frames.size()));
    }
    if (!CheckFrame(double_it_info->location.frames[0], "double_it", 6, "double_it innermost frame") ||
        !CheckFrame(double_it_info->location.frames[1], "compute", 11, "double_it outer frame")) {
        return EXIT_FAILURE;
    }

    // --- compute()'s own (non-inlined) code at 0x08000006 ---
    const model::InstructionInfo *compute_info = disassembler.InstructionInfoAt(kBase + 6);
    if (compute_info == nullptr) {
        return Fail("expected an instruction at compute's own address");
    }
    if (compute_info->location.frames.size() != 1) {
        return Fail("expected 1 frame resolving compute's own address, got " +
                    std::to_string(compute_info->location.frames.size()));
    }
    if (!CheckFrame(compute_info->location.frames[0], "compute", 13, "compute's own frame")) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// Concrete proof the mapping-symbol-guided precompute pass distinguishes
// real Thumb code from data-in-code, rather than linear-sweeping .text and
// decoding literal-pool bytes as bogus instructions. Addresses below were
// confirmed against the real firmware's symbol table before writing this
// test: `readelf -sW firmware.elf` shows a $t (Thumb) mapping symbol at
// 0x08001214, a $d (data) mapping symbol at 0x0800124c, and the next $t at
// 0x08001264 - i.e. [0x08001214, 0x0800124c) is code, [0x0800124c,
// 0x08001264) is a literal pool that must never be treated as instructions.
int CheckRealFirmwareSkipsDataInCode() {
    std::expected<disasm::ProgramDisassembler, std::string> create_result =
        disasm::ProgramDisassembler::Create(TRAILER_FIRMWARE_PATH);
    if (!create_result) {
        return Fail("firmware.elf Create failed: " + create_result.error());
    }
    const disasm::ProgramDisassembler &disassembler = *create_result;

    if (disassembler.InstructionInfoAt(0x08001214) == nullptr) {
        return Fail("expected a decoded instruction at a confirmed $t (Thumb) address (0x08001214)");
    }
    if (disassembler.InstructionInfoAt(0x0800124c) != nullptr) {
        return Fail(
            "expected NO decoded instruction at a confirmed $d (data/literal-pool) address (0x0800124c) - "
            "the precompute pass decoded data-in-code as an instruction");
    }

    return EXIT_SUCCESS;
}

}  // namespace

int main() {
    if (CheckPlainDisassembly() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (CheckSourceCorrelation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (CheckRealFirmwareSkipsDataInCode() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    std::cerr << "program_disassembler_test: OK\n";
    return EXIT_SUCCESS;
}
