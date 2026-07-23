// Deterministic smoke check for TraceDecoder: builds a known-good sample
// InstructionTraceDecodeConfig directly (mirroring what a real orchestrator
// would assemble from DAP + OpenOCD TCL data) and confirms a full decode
// run succeeds end to end (ETMv4 decoder construction + feeding the
// config's trace bytes through it) without a fatal datapath response. No
// load segments are supplied, so instruction ranges won't resolve opcodes -
// this only exercises decoder plumbing, not disassembly.

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "trace_decoder/instruction_trace_decode_config.hpp"
#include "trace_decoder/trace_decoder.hpp"
#include "trace_model/load_segment.hpp"
#include "trace_sink/trace_record.hpp"

#ifndef TRAILER_SAMPLE_ELF_PATH
#error "TRAILER_SAMPLE_ELF_PATH must be defined by the build"
#endif
#ifndef TRAILER_SAMPLE_TRACE_DUMP_PATH
#error "TRAILER_SAMPLE_TRACE_DUMP_PATH must be defined by the build"
#endif

namespace {

std::expected<std::vector<uint8_t>, std::string> ReadFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected("failed to open '" + path.string() + "'");
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        return std::unexpected("failed to determine size of '" + path.string() + "'");
    }
    std::vector<uint8_t> out(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char *>(out.data()), size)) {
        return std::unexpected("failed to read '" + path.string() + "'");
    }
    return out;
}

}  // namespace

int main(int argc, char **argv) {
    const std::filesystem::path elf_path = argc > 2 ? argv[1] : TRAILER_SAMPLE_ELF_PATH;
    const std::filesystem::path trace_dump_path = argc > 2 ? argv[2] : TRAILER_SAMPLE_TRACE_DUMP_PATH;

    const std::expected<std::vector<uint8_t>, std::string> trace_data = ReadFile(trace_dump_path);
    if (!trace_data) {
        std::cerr << "trace_decoder_test: " << trace_data.error() << "\n";
        return EXIT_FAILURE;
    }

    // ETMv4 registers + deformatter config captured from a real Cortex-M7
    // target alongside TRAILER_SAMPLE_TRACE_DUMP_PATH.
    decode::Etmv4Registers regs;
    regs.trcconfigr = 9;
    regs.trctraceidr = 1;
    regs.trcidr0 = 134219489;
    regs.trcidr1 = 1090581505;
    regs.trcidr2 = 4;
    regs.trcidr3 = 118030340;
    regs.trcidr4 = 1130496;
    regs.trcidr5 = 2428960770;
    regs.trcidr8 = 0;
    regs.trcidr9 = 0;
    regs.trcidr10 = 0;
    regs.trcidr11 = 0;
    regs.trcidr12 = 1;
    regs.trcidr13 = 0;
    regs.trcauthstatus = 192;

    decode::DeformatterConfig deformatter;
    deformatter.source_format = decode::TraceSourceFormat::kFrameFormatted;
    deformatter.frame_sync = decode::FrameSyncMode::kMemAligned;
    deformatter.reset_on_4x_fsync = true;

    decode::InstructionTraceDecodeConfig::Builder builder;
    builder.SetProgramPath(elf_path)
        .SetCoreName("Cortex-M7")
        .SetDeformatter(deformatter)
        .SetRegisters(regs)
        .SetTraceData(std::move(*trace_data));
    const decode::InstructionTraceDecodeConfig config = std::move(builder).Build();

    decode::TraceDecoder decoder;
    const std::expected<std::vector<trace::TraceRecord>, std::string> decode_result =
        decoder.Decode(config, std::span<const model::LoadSegment>{});
    if (!decode_result) {
        std::cerr << "trace_decoder_test: " << decode_result.error() << "\n";
        return EXIT_FAILURE;
    }

    std::cerr << "trace_decoder_test: OK (" << decode_result->size() << " trace records)\n";
    return EXIT_SUCCESS;
}
