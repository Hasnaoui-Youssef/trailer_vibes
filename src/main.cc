// Trailer engine driver: config -> decode tree -> raw trace bytes ->
// TraceRecords -> reconstructed (disassembled) instructions.
//
// This is a provisional listing, not the engine's final serialization
// format: it exists to exercise the pipeline built so far (disassembly +
// RTTI-split OpenCSD/LLVM modules) end to end on real trace data. A proper
// serializable results adapter is future work (see
// TRACE_ANALYSIS_IMPLEMENTATION_PLAN.md).
//
// Per CLAUDE.md: stdout carries only this machine-readable listing;
// everything else (progress, warnings, errors) goes to stderr.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <opencsd.h>

#include "config_parser/config_parser.hpp"
#include "disassembler/program_disassembler.hpp"
#include "instr_reconstruct/reconstruct.hpp"
#include "source_correlator/source_correlator.hpp"
#include "trace_decoder/decode_tree_builder.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_sink/trace_record_sink.hpp"

#ifndef TRAILER_SAMPLE_CONFIG_PATH
#error "TRAILER_SAMPLE_CONFIG_PATH must be defined by the build"
#endif

namespace {

int Fail(const std::string &message) {
    std::cerr << "trailer: " << message << "\n";
    return EXIT_FAILURE;
}

// Reads `path` fully into memory. Trace dumps in this project's test
// vectors are small (single-capture-session ETM dumps); a streaming reader
// would be the right move if that stops being true.
bool ReadFile(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        return false;
    }
    out.resize(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char *>(out.data()), size)) {
        return false;
    }
    return true;
}

// Feeds `data` through `tree` in fixed-size chunks (mirrors OpenCSD's own
// mem_buff_demo sample), then signals end-of-trace so buffered elements
// flush out. Returns the final datapath response.
ocsd_datapath_resp_t FeedTraceData(DecodeTree &tree, const std::vector<uint8_t> &data) {
    constexpr uint32_t kChunkSize = 4096;

    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    ocsd_trc_index_t index = 0;
    uint32_t bytes_remaining = static_cast<uint32_t>(data.size());

    while (OCSD_DATA_RESP_IS_CONT(resp) && bytes_remaining > 0) {
        const uint32_t block_size = std::min(bytes_remaining, kChunkSize);
        uint32_t bytes_processed = 0;
        resp = tree.TraceDataIn(OCSD_OP_DATA, index, block_size, data.data() + index, &bytes_processed);
        if (bytes_processed == 0) {
            // Shouldn't happen while resp is CONT; avoid spinning forever
            // if a decoder ever does report CONT without consuming data.
            std::cerr << "trailer: decoder stalled at trace index " << index << " without consuming data\n";
            break;
        }
        index += bytes_processed;
        bytes_remaining -= bytes_processed;
    }

    if (OCSD_DATA_RESP_IS_CONT(resp)) {
        resp = tree.TraceDataIn(OCSD_OP_EOT, index, 0, nullptr, nullptr);
    }

    return resp;
}

// Compact rendering of a full inline-frame chain, innermost first, e.g.
// "foo.c:2 (add_one) <- foo.c:10 (compute)". "??" when no debug info
// covers the address.
std::string FormatLocation(const model::SourceLocation &location) {
    if (location.frames.empty()) {
        return "??";
    }
    std::ostringstream oss;
    for (size_t i = 0; i < location.frames.size(); ++i) {
        if (i != 0) {
            oss << " <- ";
        }
        const model::InlineFrame &frame = location.frames[i];
        oss << frame.file << ":" << frame.line << " (" << (frame.function.empty() ? "?" : frame.function) << ")";
    }
    return oss.str();
}

void PrintInstruction(const model::ReconstructedInstruction &reconstructed, const model::SourceLocation &location) {
    const model::DecodedInstruction &insn = reconstructed.insn;

    std::cout << "0x" << std::hex << std::setfill('0') << std::setw(8) << insn.address << std::dec << std::setfill(' ')
               << "\t" << insn.bytes << "\t" << insn.mnemonic << "\t" << insn.operands;

    if (insn.branch_target.has_value()) {
        std::cout << "\t-> 0x" << std::hex << *insn.branch_target << std::dec;
    } else if (insn.is_return) {
        std::cout << "\t<return>";
    } else if (insn.is_indirect) {
        std::cout << "\t<indirect>";
    }
    std::cout << "\t" << FormatLocation(location) << "\n";
}

void PrintFunctionBlocks(const std::vector<model::FunctionBlock> &functions) {
    std::cout << "\n=== function / line blocks ===\n";
    for (const model::FunctionBlock &function : functions) {
        std::cout << "function " << (function.function_name.empty() ? "??" : function.function_name) << " @ 0x"
                   << std::hex << function.entry_addr << std::dec << "\n";
        for (const model::LineBlock &line_block : function.line_blocks) {
            std::cout << "  [0x" << std::hex << line_block.start_addr << "-0x" << line_block.end_addr << std::dec
                       << "] (" << line_block.instr_count << " instr) " << FormatLocation(line_block.location)
                       << "\n";
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    const std::string config_path = argc > 1 ? argv[1] : TRAILER_SAMPLE_CONFIG_PATH;

    config::ConfigParser parser;
    const config::ParseResult parse_result = parser.ParseJson(config_path);
    if (!parse_result.Ok()) {
        return Fail(parse_result.error);
    }
    const config::PipelineConfig &config = *parse_result.config;

    disasm::CreateResult disasm_result = disasm::ProgramDisassembler::Create(config.program_path);
    if (!disasm_result.Ok()) {
        return Fail(disasm_result.error);
    }
    disasm::ProgramDisassembler &disassembler = *disasm_result.disassembler;

    correlate::CreateResult correlate_result = correlate::SourceCorrelator::Create(config.program_path);
    if (!correlate_result.Ok()) {
        return Fail(correlate_result.error);
    }
    correlate::SourceCorrelator &correlator = *correlate_result.correlator;

    trace::TraceRecordSink sink;
    decode::DecodeTreeBuilder builder;
    const decode::BuildResult build_result = builder.Build(config, disassembler.load_segments(), sink);
    if (!build_result.Ok()) {
        return Fail(build_result.error);
    }

    std::vector<uint8_t> trace_data;
    if (!ReadFile(config.trace_dump_path, trace_data)) {
        return Fail("failed to read trace dump '" + config.trace_dump_path + "'");
    }

    const ocsd_datapath_resp_t decode_resp = FeedTraceData(*build_result.tree, trace_data);
    if (OCSD_DATA_RESP_IS_FATAL(decode_resp)) {
        return Fail("decode failed with fatal datapath response " + std::to_string(static_cast<int>(decode_resp)));
    }
    if (OCSD_DATA_RESP_IS_WARN_OR_ERR(decode_resp)) {
        std::cerr << "trailer: decode completed with warnings/errors (datapath response "
                   << static_cast<int>(decode_resp) << ")\n";
    }

    const std::vector<model::ReconstructedInstruction> instructions =
        reconstruct::Reconstruct(sink.records(), disassembler);

    for (const model::ReconstructedInstruction &instruction : instructions) {
        if (!instruction.executed) {
            continue;
        }
        PrintInstruction(instruction, correlator.Resolve(instruction.insn.address));
    }

    PrintFunctionBlocks(correlator.Correlate(instructions));

    std::cerr << "trailer: " << sink.records().size() << " trace records, " << instructions.size()
               << " reconstructed instructions\n";

    return EXIT_SUCCESS;
}
