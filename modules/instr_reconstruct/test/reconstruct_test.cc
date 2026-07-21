// Deterministic unit test for reconstruct::Reconstruct: hand-builds
// TraceRecord instruction ranges over the disassembler module's fixture.elf
// (see modules/disassembler/test/fixture.s) and checks that the OpenCSD
// side (TraceRecord) is bridged to the LLVM side (ProgramDisassembler)
// correctly - in particular, that TraceRecord::last_instr_executed only
// suppresses the *last* instruction of its own range, not any other.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "instr_reconstruct/reconstruct.hpp"

#ifndef TRAILER_DISASSEMBLER_FIXTURE_PATH
#error "TRAILER_DISASSEMBLER_FIXTURE_PATH must be defined by the build"
#endif

namespace {

constexpr uint64_t kBase = 0x08000000;

int Fail(const std::string &message) {
    std::cerr << "reconstruct_test: " << message << "\n";
    return EXIT_FAILURE;
}

trace::TraceRecord MakeRange(ocsd_trc_index_t index_sop, ocsd_vaddr_t start_addr, ocsd_vaddr_t end_addr,
                             bool last_instr_executed) {
    trace::TraceRecord record{};
    record.index_sop = index_sop;
    record.trace_id = 1;
    record.kind = trace::TraceRecordKind::kInstructionRange;
    record.isa = ocsd_isa_thumb2;
    record.start_addr = start_addr;
    record.end_addr = end_addr;
    record.has_end_addr = true;
    record.last_instr_executed = last_instr_executed;
    return record;
}

}  // namespace

int main() {
    const disasm::CreateResult create_result = disasm::ProgramDisassembler::Create(TRAILER_DISASSEMBLER_FIXTURE_PATH);
    if (!create_result.Ok()) {
        return Fail("Create failed: " + create_result.error);
    }

    // record1 covers 'add sp,sp,#16' + 'bl target' (both executed).
    // record2 covers just 'bx lr', with last_instr_executed = false, to
    // check that the "not executed" flag lands on the right instruction
    // and doesn't leak into an unrelated range.
    const std::vector<trace::TraceRecord> records = {
        MakeRange(100, kBase, kBase + 0x6, /*last_instr_executed=*/true),
        MakeRange(101, kBase + 0x6, kBase + 0x8, /*last_instr_executed=*/false),
    };

    const std::vector<model::ReconstructedInstruction> instrs =
        reconstruct::Reconstruct(records, *create_result.disassembler);

    if (instrs.size() != 3) {
        return Fail("expected 3 reconstructed instructions, got " + std::to_string(instrs.size()));
    }

    if (instrs[0].insn.mnemonic != "add" || instrs[0].trace_index != 100 || !instrs[0].executed) {
        return Fail("unexpected 'add' reconstruction");
    }
    if (instrs[1].insn.mnemonic != "bl" || instrs[1].trace_index != 100 || !instrs[1].executed) {
        return Fail("unexpected 'bl' reconstruction");
    }
    if (instrs[2].insn.mnemonic != "bx" || instrs[2].trace_index != 101) {
        return Fail("unexpected 'bx' reconstruction");
    }
    if (instrs[2].executed) {
        return Fail("'bx' should be marked not-executed (its range's last_instr_executed was false)");
    }
    if (instrs[2].trace_id != 1) {
        return Fail("unexpected trace_id on 'bx' reconstruction");
    }

    std::cerr << "reconstruct_test: OK\n";
    return EXIT_SUCCESS;
}
