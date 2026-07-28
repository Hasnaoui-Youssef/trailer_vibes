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

#include "trace_model/instruction_trace_decode_config.hpp"
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
    model::Etmv4Registers regs;
    regs.trcconfigr = 0x00000009;
    regs.trctraceidr = 0x00000001;
    regs.trcidr0 = 0x080006E1;
    regs.trcidr1 = 0x4100F401;
    regs.trcidr2 = 0x00000004;
    regs.trcidr3 = 0x07090004;
    regs.trcidr4 = 0x00114000;
    regs.trcidr5 = 0x90C70002;
    regs.trcidr8 = 0x00000000;
    regs.trcidr9 = 0x00000000;
    regs.trcidr10 = 0x00000000;
    regs.trcidr11 = 0x00000000;
    regs.trcidr12 = 0x00000001;
    regs.trcidr13 = 0x00000000;
    regs.trcauthstatus = 0x000000C0;

    model::DeformatterConfig deformatter;
    deformatter.source_format = model::TraceSourceFormat::kFrameFormatted;
    deformatter.frame_sync = model::FrameSyncMode::kMemAligned;
    deformatter.reset_on_4x_fsync = true;

    model::InstructionTraceDecodeConfig::Builder builder;
    builder.SetProgramPath(elf_path)
        .SetCoreName("Cortex-M7")
        .SetDeformatter(deformatter)
        .SetRegisters(regs)
        .SetTraceData(std::move(*trace_data));
    const model::InstructionTraceDecodeConfig config = std::move(builder).Build();

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
