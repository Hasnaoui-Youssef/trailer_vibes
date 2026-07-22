// Deterministic unit test for the trace_transform module: both
// TraceTransform<T> specializations (ReconstructedInstruction expansion,
// FunctionBlock grouping) driven entirely by a hand-built in-memory
// instruction table - no ELF/LLVM disassembler involved at all. That's the
// point: TraceTransform<T> depends only on trace_model + trace_sink, so
// unit-testing it never requires loading a real firmware image.
//
// Covers: reconstruct_test.cc's original last_instr_executed case,
// source_correlator_test.cc's original inline-chain-split/same-function-
// merge case, a new assembly-file-never-merges case, and the
// TransformableFrom concept correctly rejecting an unspecialized type.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "trace_model/function_block.hpp"
#include "trace_model/instruction_info.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_sink/trace_record.hpp"
#include "trace_transform/function_block_transform.hpp"
#include "trace_transform/reconstructed_instruction_transform.hpp"
#include "trace_transform/transform.hpp"

namespace {

int Fail(const std::string &message) {
    std::cerr << "transform_test: " << message << "\n";
    return EXIT_FAILURE;
}

// A tiny in-memory instruction table + resolve callable.
class FakeInstructionTable {
public:
    void Add(model::InstructionInfo info) {
        const uint64_t address = info.insn.address;
        table_.emplace(address, std::move(info));
    }

    const model::InstructionInfo *operator()(uint64_t address) const {
        const auto it = table_.find(address);
        return it == table_.end() ? nullptr : &it->second;
    }

private:
    std::map<uint64_t, model::InstructionInfo> table_;
};

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

model::InstructionInfo MakeInsn(uint64_t address, uint8_t size, std::string mnemonic) {
    model::InstructionInfo info{};
    info.insn.address = address;
    info.insn.size = size;
    info.insn.isa = model::InstructionSet::kThumb;
    info.insn.mnemonic = std::move(mnemonic);
    return info;
}

model::InlineFrame MakeFrame(std::string function, std::string file, uint32_t line) {
    model::InlineFrame frame;
    frame.function = std::move(function);
    frame.file = std::move(file);
    frame.line = line;
    frame.column = 0;
    return frame;
}

// --- reconstruct_test.cc's original case: last_instr_executed must only
//     suppress the LAST instruction of its OWN range, never leak into an
//     unrelated one. ---
int CheckReconstruction() {
    constexpr uint64_t kBase = 0x08000000;

    FakeInstructionTable table;
    table.Add(MakeInsn(kBase, 2, "add"));
    table.Add(MakeInsn(kBase + 2, 4, "bl"));
    table.Add(MakeInsn(kBase + 6, 2, "bx"));

    const std::vector<trace::TraceRecord> records = {
        MakeRange(100, kBase, kBase + 0x6, /*last_instr_executed=*/true),
        MakeRange(101, kBase + 0x6, kBase + 0x8, /*last_instr_executed=*/false),
    };

    const std::vector<model::ReconstructedInstruction> instrs =
        xform::Transform<model::ReconstructedInstruction>(records, table);

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

    return EXIT_SUCCESS;
}

// --- source_correlator_test.cc's original case: full inline chain split +
//     same-function merge, now driven by a fake in-memory table instead of
//     a real ELF/DWARFContext. ---
int CheckFunctionBlockGrouping() {
    constexpr uint64_t kBase = 0x08000000;

    FakeInstructionTable table;

    model::InstructionInfo add_one_insn = MakeInsn(kBase, 2, "adds");
    add_one_insn.location.frames = {MakeFrame("add_one", "fixture.c", 2), MakeFrame("compute", "fixture.c", 10)};
    table.Add(add_one_insn);

    model::InstructionInfo double_it_insn = MakeInsn(kBase + 2, 4, "add.w");
    double_it_insn.location.frames = {MakeFrame("double_it", "fixture.c", 6), MakeFrame("compute", "fixture.c", 11)};
    table.Add(double_it_insn);

    model::InstructionInfo compute_insn1 = MakeInsn(kBase + 6, 2, "add");
    compute_insn1.location.frames = {MakeFrame("compute", "fixture.c", 13)};
    table.Add(compute_insn1);

    model::InstructionInfo compute_insn2 = MakeInsn(kBase + 8, 2, "bx");
    compute_insn2.location.frames = {MakeFrame("compute", "fixture.c", 13)};  // Same location as compute_insn1.
    table.Add(compute_insn2);

    const std::vector<trace::TraceRecord> records = {
        MakeRange(1, kBase, kBase + 0xa, /*last_instr_executed=*/true),
    };

    const std::vector<model::FunctionBlock> functions = xform::Transform<model::FunctionBlock>(records, table);

    if (functions.size() != 1) {
        return Fail("expected 1 function block, got " + std::to_string(functions.size()));
    }
    const model::FunctionBlock &compute_block = functions.front();
    if (compute_block.function_name != "compute" || compute_block.entry_addr != kBase) {
        return Fail("unexpected function block: name='" + compute_block.function_name + "'");
    }
    if (compute_block.line_blocks.size() != 3) {
        return Fail("expected 3 line blocks, got " + std::to_string(compute_block.line_blocks.size()));
    }

    const model::LineBlock &lb0 = compute_block.line_blocks[0];
    if (lb0.start_addr != kBase || lb0.end_addr != kBase + 2 || lb0.instr_count != 1) {
        return Fail("unexpected add_one line block bounds");
    }
    const model::LineBlock &lb1 = compute_block.line_blocks[1];
    if (lb1.start_addr != kBase + 2 || lb1.end_addr != kBase + 6 || lb1.instr_count != 1) {
        return Fail("unexpected double_it line block bounds");
    }
    const model::LineBlock &lb2 = compute_block.line_blocks[2];
    if (lb2.start_addr != kBase + 6 || lb2.end_addr != kBase + 0xa || lb2.instr_count != 2) {
        return Fail("unexpected compute@13 line block bounds (should merge both trailing instructions)");
    }

    return EXIT_SUCCESS;
}

// --- new case: locations resolving to an assembly file never merge line
//     blocks, even when consecutive instructions share the exact same
//     (file,line) - unlike the C-file case above, which does merge. ---
int CheckAssemblyLineBlocksNeverMerge() {
    constexpr uint64_t kBase = 0x08000000;

    FakeInstructionTable table;
    for (int i = 0; i < 3; ++i) {
        model::InstructionInfo insn = MakeInsn(kBase + static_cast<uint64_t>(i) * 2, 2, "nop");
        // All three share the SAME (file,line) - the case that would merge
        // into one LineBlock for a C file, but must never merge here.
        insn.location.frames = {MakeFrame("Reset_Handler", "startup.s", 42)};
        table.Add(insn);
    }

    const std::vector<trace::TraceRecord> records = {
        MakeRange(1, kBase, kBase + 0x6, /*last_instr_executed=*/true),
    };

    const std::vector<model::FunctionBlock> functions = xform::Transform<model::FunctionBlock>(records, table);

    if (functions.size() != 1) {
        return Fail("expected 1 function block, got " + std::to_string(functions.size()));
    }
    if (functions.front().line_blocks.size() != 3) {
        return Fail("expected 3 separate line blocks for 3 same-location .s instructions (asm never merges), got " +
                    std::to_string(functions.front().line_blocks.size()));
    }
    for (const model::LineBlock &line_block : functions.front().line_blocks) {
        if (line_block.instr_count != 1) {
            return Fail("expected every asm line block to have exactly 1 instruction");
        }
    }

    return EXIT_SUCCESS;
}

// A type with no TraceTransform<T> specialization at all.
struct UnspecializedDummyType {};

}  // namespace

static_assert(xform::TransformableFrom<model::ReconstructedInstruction, FakeInstructionTable>);
static_assert(xform::TransformableFrom<model::FunctionBlock, FakeInstructionTable>);
static_assert(!xform::TransformableFrom<UnspecializedDummyType, FakeInstructionTable>);

int main() {
    if (CheckReconstruction() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (CheckFunctionBlockGrouping() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    if (CheckAssemblyLineBlocksNeverMerge() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    std::cerr << "transform_test: OK\n";
    return EXIT_SUCCESS;
}
