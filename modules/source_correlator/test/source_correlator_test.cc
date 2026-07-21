// Deterministic unit test for SourceCorrelator: resolves addresses in a
// tiny, pre-built firmware image with forced inlining (fixture.elf, see
// fixture.c) and checks both single-address resolution (full inline-frame
// chain) and Correlate()'s line/function block grouping.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "source_correlator/source_correlator.hpp"

#ifndef TRAILER_SOURCE_CORRELATOR_FIXTURE_PATH
#error "TRAILER_SOURCE_CORRELATOR_FIXTURE_PATH must be defined by the build"
#endif

namespace {

int Fail(const std::string &message) {
    std::cerr << "source_correlator_test: " << message << "\n";
    return EXIT_FAILURE;
}

bool CheckFrame(const model::InlineFrame &frame, const std::string &function, uint32_t line,
                const std::string &what) {
    if (frame.function != function || frame.file != "fixture.c" || frame.line != line) {
        std::cerr << "source_correlator_test: " << what << ": got function='" << frame.function << "' file='"
                   << frame.file << "' line=" << frame.line << ", expected function='" << function
                   << "' file='fixture.c' line=" << line << "\n";
        return false;
    }
    return true;
}

model::ReconstructedInstruction MakeInstr(uint64_t address, uint8_t size) {
    model::ReconstructedInstruction instr{};
    instr.insn.address = address;
    instr.insn.size = size;
    instr.trace_index = address;  // Arbitrary but distinct, unused by Correlate().
    instr.trace_id = 1;
    instr.executed = true;
    return instr;
}

}  // namespace

int main() {
    const correlate::CreateResult create_result =
        correlate::SourceCorrelator::Create(TRAILER_SOURCE_CORRELATOR_FIXTURE_PATH);
    if (!create_result.Ok()) {
        return Fail("Create failed: " + create_result.error);
    }
    const correlate::SourceCorrelator &correlator = *create_result.correlator;

    // --- Resolve(): inlined add_one() at 0x08000000, called from compute() at fixture.c:10 ---
    const model::SourceLocation add_one_loc = correlator.Resolve(0x08000000);
    if (add_one_loc.frames.size() != 2) {
        return Fail("expected 2 frames resolving add_one's address, got " + std::to_string(add_one_loc.frames.size()));
    }
    if (!CheckFrame(add_one_loc.frames[0], "add_one", 2, "add_one innermost frame") ||
        !CheckFrame(add_one_loc.frames[1], "compute", 10, "add_one outer frame")) {
        return EXIT_FAILURE;
    }

    // --- Resolve(): inlined double_it() at 0x08000002, called from compute() at fixture.c:11 ---
    const model::SourceLocation double_it_loc = correlator.Resolve(0x08000002);
    if (double_it_loc.frames.size() != 2) {
        return Fail("expected 2 frames resolving double_it's address, got " +
                    std::to_string(double_it_loc.frames.size()));
    }
    if (!CheckFrame(double_it_loc.frames[0], "double_it", 6, "double_it innermost frame") ||
        !CheckFrame(double_it_loc.frames[1], "compute", 11, "double_it outer frame")) {
        return EXIT_FAILURE;
    }

    // --- Resolve(): compute()'s own (non-inlined) code at 0x08000006 ---
    const model::SourceLocation compute_loc = correlator.Resolve(0x08000006);
    if (compute_loc.frames.size() != 1) {
        return Fail("expected 1 frame resolving compute's own address, got " +
                    std::to_string(compute_loc.frames.size()));
    }
    if (!CheckFrame(compute_loc.frames[0], "compute", 13, "compute's own frame")) {
        return EXIT_FAILURE;
    }

    // --- Correlate(): all 4 addresses are physically inside compute() (the
    //     first two via inlining), so this must produce ONE function block
    //     with THREE line blocks: [add_one chain], [double_it chain],
    //     [compute@13 - merging both trailing instructions since they share
    //     the same full inline chain].
    const std::vector<model::ReconstructedInstruction> instructions = {
        MakeInstr(0x08000000, 2),  // add_one (inlined)
        MakeInstr(0x08000002, 4),  // double_it (inlined)
        MakeInstr(0x08000006, 2),  // compute's own "add r0, r1"
        MakeInstr(0x08000008, 2),  // compute's own "bx lr"
    };

    const std::vector<model::FunctionBlock> functions = correlator.Correlate(instructions);
    if (functions.size() != 1) {
        return Fail("expected 1 function block, got " + std::to_string(functions.size()));
    }
    const model::FunctionBlock &compute_block = functions.front();
    if (compute_block.function_name != "compute" || compute_block.entry_addr != 0x08000000) {
        return Fail("unexpected function block: name='" + compute_block.function_name + "'");
    }
    if (compute_block.line_blocks.size() != 3) {
        return Fail("expected 3 line blocks, got " + std::to_string(compute_block.line_blocks.size()));
    }

    const model::LineBlock &lb0 = compute_block.line_blocks[0];
    if (lb0.start_addr != 0x08000000 || lb0.end_addr != 0x08000002 || lb0.instr_count != 1) {
        return Fail("unexpected add_one line block bounds");
    }
    const model::LineBlock &lb1 = compute_block.line_blocks[1];
    if (lb1.start_addr != 0x08000002 || lb1.end_addr != 0x08000006 || lb1.instr_count != 1) {
        return Fail("unexpected double_it line block bounds");
    }
    const model::LineBlock &lb2 = compute_block.line_blocks[2];
    if (lb2.start_addr != 0x08000006 || lb2.end_addr != 0x0800000a || lb2.instr_count != 2) {
        return Fail("unexpected compute@13 line block bounds (should merge both trailing instructions)");
    }

    std::cerr << "source_correlator_test: OK\n";
    return EXIT_SUCCESS;
}
