// Regression oracle for the RTTI seam: effectively the retired `trailer`
// CLI's main(), now exercising trace_provider::TraceSession instead of
// wiring trace_decoder/trace_transform directly. Counts below were frozen
// from this test's first green run against test_resources/etm_dump.bin.

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "disassembler/program_disassembler.hpp"
#include "trace_model/instruction_trace_decode_config.hpp"
#include "trace_provider/trace_session.hpp"

#ifndef TRAILER_FIRMWARE_PATH
#error "TRAILER_FIRMWARE_PATH must be defined by the build"
#endif
#ifndef TRAILER_TRACE_DUMP_PATH
#error "TRAILER_TRACE_DUMP_PATH must be defined by the build"
#endif
#ifndef TRAILER_ETM_REGS_JSON_PATH
#error "TRAILER_ETM_REGS_JSON_PATH must be defined by the build"
#endif

namespace {

int Fail(const std::string &message) {
    std::cerr << "trace_provider_test: " << message << "\n";
    return EXIT_FAILURE;
}

std::expected<std::vector<std::byte>, std::string> ReadFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::unexpected("failed to open '" + path.string() + "'");
    const std::streamsize size = file.tellg();
    if (size < 0) return std::unexpected("failed to determine size of '" + path.string() + "'");
    std::vector<std::byte> out(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char *>(out.data()), size))
        return std::unexpected("failed to read '" + path.string() + "'");
    return out;
}

model::Etmv4Registers LoadRegisters(const std::filesystem::path &path) {
    std::ifstream file(path);
    nlohmann::json j;
    file >> j;

    model::Etmv4Registers regs;
    regs.trcconfigr = j.at("TRCCONFIGR").get<uint32_t>();
    regs.trctraceidr = j.at("TRCTRACEIDR").get<uint32_t>();
    regs.trcidr0 = j.at("TRCIDR0").get<uint32_t>();
    regs.trcidr1 = j.at("TRCIDR1").get<uint32_t>();
    regs.trcidr2 = j.at("TRCIDR2").get<uint32_t>();
    regs.trcidr3 = j.at("TRCIDR3").get<uint32_t>();
    regs.trcidr4 = j.at("TRCIDR4").get<uint32_t>();
    regs.trcidr5 = j.at("TRCIDR5").get<uint32_t>();
    regs.trcidr6 = j.at("TRCIDR6").get<uint32_t>();
    regs.trcidr7 = j.at("TRCIDR7").get<uint32_t>();
    regs.trcidr8 = j.at("TRCIDR8").get<uint32_t>();
    regs.trcidr9 = j.at("TRCIDR9").get<uint32_t>();
    regs.trcidr10 = j.at("TRCIDR10").get<uint32_t>();
    regs.trcidr11 = j.at("TRCIDR11").get<uint32_t>();
    regs.trcidr12 = j.at("TRCIDR12").get<uint32_t>();
    regs.trcidr13 = j.at("TRCIDR13").get<uint32_t>();
    regs.trcauthstatus = j.at("TRCAUTHSTATUS").get<uint32_t>();
    return regs;
}

}  // namespace

int main() {
    std::expected<std::vector<std::byte>, std::string> trace_data = ReadFile(TRAILER_TRACE_DUMP_PATH);
    if (!trace_data) return Fail(trace_data.error());

    std::expected<disasm::ProgramDisassembler, std::string> disasm_result =
        disasm::ProgramDisassembler::Create(TRAILER_FIRMWARE_PATH);
    if (!disasm_result) return Fail(disasm_result.error());
    disasm::ProgramDisassembler &disassembler = *disasm_result;

    model::DeformatterConfig deformatter;
    deformatter.source_format = model::TraceSourceFormat::kFrameFormatted;
    deformatter.frame_sync = model::FrameSyncMode::kMemAligned;
    deformatter.reset_on_4x_fsync = true;

    model::InstructionTraceDecodeConfig::Builder builder;
    builder.SetProgramPath(TRAILER_FIRMWARE_PATH)
        .SetCoreName("Cortex-M7")
        .SetDeformatter(deformatter)
        .SetRegisters(LoadRegisters(TRAILER_ETM_REGS_JSON_PATH));
    model::InstructionTraceDecodeConfig base_config = std::move(builder).Build();

    auto resolve = [&](uint64_t address) { return disassembler.InstructionInfoAt(address); };

    trace_provider::TraceSession session(std::move(base_config), disassembler.load_segments(), resolve);
    std::expected<trace_provider::DecodedIncrement, std::string> increment = session.Append(*trace_data);
    if (!increment) return Fail(increment.error());

    if (increment->instructions.empty()) return Fail("decoded zero executed instructions");

    for (const model::ReconstructedInstruction &instruction : increment->instructions) {
        if (disassembler.InstructionInfoAt(instruction.insn.address) == nullptr) {
            return Fail("instruction at 0x" + std::to_string(instruction.insn.address) +
                        " does not resolve in-image");
        }
    }

    bool saw_named_function = false;
    for (const model::FunctionBlock &block : increment->function_blocks) {
        if (!block.function_name.empty()) {
            saw_named_function = true;
            break;
        }
    }
    if (!saw_named_function) return Fail("no FunctionBlock has a resolved function name");

    std::cout << "trace_provider_test: " << increment->instructions.size() << " executed instructions, "
              << increment->function_blocks.size() << " function blocks, " << increment->gaps.size() << " gaps\n";

    if (increment->instructions.size() != 114) {
        return Fail("expected 114 executed instructions (frozen oracle), got " +
                    std::to_string(increment->instructions.size()));
    }
    if (increment->function_blocks.size() != 6) {
        return Fail("expected 6 function blocks (frozen oracle), got " +
                    std::to_string(increment->function_blocks.size()));
    }
    if (increment->gaps.size() != 2) {
        return Fail("expected 2 gaps (frozen oracle), got " + std::to_string(increment->gaps.size()));
    }

    return EXIT_SUCCESS;
}
