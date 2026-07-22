// Trailer engine driver: config -> disassemble (+ precompute source
// correlation) -> decode -> transform into the views this provisional
// listing wants.
//
// This is a provisional listing, not the engine's final serialization
// format: it exists to exercise the pipeline built so far end to end on
// real trace data. A proper serializable results adapter is future work
// (see TRACE_ANALYSIS_IMPLEMENTATION_PLAN.md). Likewise, main() plays the
// role a future Orchestrator will take over (owning pipeline artifacts and
// chaining stages from a described behavior) - see that plan for the
// design intent: every derived view below is produced via
// xform::Transform<T>, not a bespoke per-view function, so a future
// Orchestrator can drive the same calls from a declarative description
// without this file's shape changing.
//
// Per CLAUDE.md: stdout carries only this machine-readable listing;
// everything else (progress, warnings, errors) goes to stderr.
//
// Notably absent from this file: any OpenCSD type or header beyond
// trace::TraceRecord (transport-neutral vocabulary), and any LLVM type
// beyond disassembler's LLVM-free interface. Decoding is entirely private
// to trace_decoder::TraceDecoder; disassembly and source correlation are
// entirely private to disasm::ProgramDisassembler, precomputed once and
// queried here only via InstructionInfoAt.

#include <cstdlib>
#include <expected>
#include <format>
#include <print>
#include <string>
#include <vector>

#include "config_parser/config_parser.hpp"
#include "disassembler/program_disassembler.hpp"
#include "trace_decoder/trace_decoder.hpp"
#include "trace_model/function_block.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_sink/trace_record.hpp"
#include "trace_transform/function_block_transform.hpp"
#include "trace_transform/reconstructed_instruction_transform.hpp"
#include "trace_transform/transform.hpp"

#ifndef TRAILER_SAMPLE_CONFIG_PATH
#error "TRAILER_SAMPLE_CONFIG_PATH must be defined by the build"
#endif

namespace {

int Fail(const std::string &message) {
    std::println(stderr, "trailer: {}", message);
    return EXIT_FAILURE;
}

// Compact rendering of a full inline-frame chain, innermost first, e.g.
// "foo.c:2 (add_one) <- foo.c:10 (compute)". "??" when no debug info
// covers the address.
std::string FormatLocation(const model::SourceLocation &location) {
    if (location.frames.empty()) {
        return "??";
    }
    std::string out;
    for (size_t i = 0; i < location.frames.size(); ++i) {
        if (i != 0) {
            out += " <- ";
        }
        const model::InlineFrame &frame = location.frames[i];
        out += std::format("{}:{} ({})", frame.file, frame.line, frame.function.empty() ? "?" : frame.function);
    }
    return out;
}

void PrintInstruction(const model::ReconstructedInstruction &reconstructed, const model::SourceLocation &location) {
    const model::DecodedInstruction &insn = reconstructed.insn;

    std::string flags;
    if (insn.branch_target.has_value()) {
        flags = std::format("\t-> 0x{:x}", *insn.branch_target);
    } else if (insn.is_return) {
        flags = "\t<return>";
    } else if (insn.is_indirect) {
        flags = "\t<indirect>";
    }

    std::println("0x{:08x}\t{}\t{}\t{}{}\t{}", insn.address, insn.bytes, insn.mnemonic, insn.operands, flags,
                  FormatLocation(location));
}

void PrintFunctionBlocks(const std::vector<model::FunctionBlock> &functions) {
    std::println();
    std::println("=== function / line blocks ===");
    for (const model::FunctionBlock &function : functions) {
        std::println("function {} @ 0x{:x}", function.function_name.empty() ? "??" : function.function_name,
                      function.entry_addr);
        for (const model::LineBlock &line_block : function.line_blocks) {
            std::println("  [0x{:x}-0x{:x}] ({} instr) {}", line_block.start_addr, line_block.end_addr,
                          line_block.instr_count, FormatLocation(line_block.location));
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    const std::string config_path = argc > 1 ? argv[1] : TRAILER_SAMPLE_CONFIG_PATH;

    config::ConfigParser parser;
    const std::expected<config::PipelineConfig, std::string> parse_result = parser.ParseJson(config_path);
    if (!parse_result) {
        return Fail(parse_result.error());
    }
    const config::PipelineConfig &config = *parse_result;

    std::expected<disasm::ProgramDisassembler, std::string> disasm_result =
        disasm::ProgramDisassembler::Create(config.program_path);
    if (!disasm_result) {
        return Fail(disasm_result.error());
    }
    disasm::ProgramDisassembler &disassembler = *disasm_result;

    decode::TraceDecoder decoder;
    const std::expected<std::vector<trace::TraceRecord>, std::string> decode_result =
        decoder.Decode(config, disassembler.load_segments());
    if (!decode_result) {
        return Fail(decode_result.error());
    }
    const std::vector<trace::TraceRecord> &records = *decode_result;

    // The one lookup every derived view below is built from: disassembly +
    // source correlation are already precomputed inside `disassembler`
    // (see ProgramDisassembler::Create) - this is just a cheap table query.
    auto resolve = [&](uint64_t address) { return disassembler.InstructionInfoAt(address); };

    const std::vector<model::ReconstructedInstruction> instructions =
        xform::Transform<model::ReconstructedInstruction>(records, resolve);
    for (const model::ReconstructedInstruction &instruction : instructions) {
        if (!instruction.executed) {
            continue;
        }
        const model::InstructionInfo *info = resolve(instruction.insn.address);
        PrintInstruction(instruction, info != nullptr ? info->location : model::SourceLocation{});
    }

    PrintFunctionBlocks(xform::Transform<model::FunctionBlock>(records, resolve));

    std::println(stderr, "trailer: {} trace records, {} reconstructed instructions", records.size(),
                  instructions.size());

    return EXIT_SUCCESS;
}
