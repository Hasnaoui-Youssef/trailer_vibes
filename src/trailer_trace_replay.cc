// Offline: ELF + captured bytes + ETM registers -> decode -> reconstruct, no board needed.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "device_xml/svd_loader.hpp"
#include "disassembler/program_disassembler.hpp"
#include "trace_decoder/trace_decoder.hpp"
#include "trace_exception_attribution/exception_attribution.hpp"
#include "trace_model/function_block.hpp"
#include "trace_model/instruction_trace_decode_config.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_model/exception_event.hpp"
#include "trace_model/trace_gap.hpp"
#include "trace_sink/trace_record.hpp"
#include "trace_transform/exception_event_transform.hpp"
#include "trace_transform/function_block_transform.hpp"
#include "trace_transform/reconstructed_instruction_transform.hpp"
#include "trace_transform/trace_gap_transform.hpp"
#include "trace_transform/transform.hpp"

#ifndef TRAILER_SAMPLE_ELF_PATH
#error "TRAILER_SAMPLE_ELF_PATH must be defined by the build"
#endif
#ifndef TRAILER_SAMPLE_TRACE_DUMP_PATH
#error "TRAILER_SAMPLE_TRACE_DUMP_PATH must be defined by the build"
#endif

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int Fail(const std::string &message) {
    std::println(stderr, "trailer-trace-replay: {}", message);
    return EXIT_FAILURE;
}

struct Args {
    std::filesystem::path elf_path = TRAILER_SAMPLE_ELF_PATH;
    std::filesystem::path capture_path = TRAILER_SAMPLE_TRACE_DUMP_PATH;
    std::optional<std::filesystem::path> etm_regs_path;
    std::optional<std::filesystem::path> svd_path;
    std::string format = "text";
    std::optional<std::filesystem::path> metrics_path;
    bool dump_elements = false;
};

void PrintUsage(std::string_view argv0) {
    std::println(stderr,
                  "usage: {} [--elf PATH] [--capture PATH] [--etm-regs PATH.json] [--svd PATH.svd]\n"
                  "           [--format text|json] [--metrics PATH.json] [--dump-elements]",
                  argv0);
}

std::optional<Args> ParseArgs(int argc, char **argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto next = [&]() -> std::optional<std::string_view> {
            if (i + 1 >= argc) return std::nullopt;
            return argv[++i];
        };
        if (arg == "--elf") {
            auto v = next();
            if (!v) return std::nullopt;
            args.elf_path = *v;
        } else if (arg == "--capture") {
            auto v = next();
            if (!v) return std::nullopt;
            args.capture_path = *v;
        } else if (arg == "--etm-regs") {
            auto v = next();
            if (!v) return std::nullopt;
            args.etm_regs_path = std::filesystem::path(*v);
        } else if (arg == "--svd") {
            auto v = next();
            if (!v) return std::nullopt;
            args.svd_path = std::filesystem::path(*v);
        } else if (arg == "--format") {
            auto v = next();
            if (!v) return std::nullopt;
            args.format = std::string(*v);
        } else if (arg == "--metrics") {
            auto v = next();
            if (!v) return std::nullopt;
            args.metrics_path = std::filesystem::path(*v);
        } else if (arg == "--dump-elements") {
            args.dump_elements = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return std::nullopt;
        } else {
            std::println(stderr, "trailer-trace-replay: unrecognized argument '{}'", arg);
            return std::nullopt;
        }
    }
    if (args.format != "text" && args.format != "json") {
        std::println(stderr, "trailer-trace-replay: --format must be 'text' or 'json'");
        return std::nullopt;
    }
    return args;
}

std::expected<std::vector<uint8_t>, std::string> ReadFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(std::format("failed to open '{}'", path.string()));
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        return std::unexpected(std::format("failed to determine size of '{}'", path.string()));
    }
    std::vector<uint8_t> out(static_cast<size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char *>(out.data()), size)) {
        return std::unexpected(std::format("failed to read '{}'", path.string()));
    }
    return out;
}

// Matches test_resources/etm_dump.bin's golden log; --etm-regs overrides with a real capture.
model::Etmv4Registers DefaultEtmv4Registers() {
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
    return regs;
}

std::expected<model::Etmv4Registers, std::string> LoadEtmv4Registers(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file) {
        return std::unexpected(std::format("failed to open '{}'", path.string()));
    }
    nlohmann::json j;
    try {
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
    } catch (const nlohmann::json::exception &error) {
        return std::unexpected(std::format("malformed ETM register JSON '{}': {}", path.string(), error.what()));
    }
}

// TRCCONFIGR field positions, see external/openocd/src/target/arm_etmv4.h.
constexpr uint32_t kTrcconfigrInstp0Shift = 1;
constexpr uint32_t kTrcconfigrInstp0Mask = 0x3;
constexpr uint32_t kTrcconfigrCondShift = 8;
constexpr uint32_t kTrcconfigrCondMask = 0x7;

// OpenCSD hard-rejects COND/INSTP0 unconditionally (OCSD_ERR_HW_CFG_UNSUPP).
std::optional<std::string> CheckConfigLimits(const model::Etmv4Registers &regs) {
    const uint32_t cond = (regs.trcconfigr >> kTrcconfigrCondShift) & kTrcconfigrCondMask;
    const uint32_t instp0 = (regs.trcconfigr >> kTrcconfigrInstp0Shift) & kTrcconfigrInstp0Mask;
    if (cond != 0) {
        return std::format(
            "TRCCONFIGR.COND = {} (conditional instruction tracing enabled): "
            "OpenCSD rejects this configuration unconditionally (OCSD_ERR_HW_CFG_UNSUPP)",
            cond);
    }
    if (instp0 != 0) {
        return std::format(
            "TRCCONFIGR.INSTP0 = {} (load/store-as-P0 tracing enabled): "
            "OpenCSD rejects this configuration unconditionally (OCSD_ERR_HW_CFG_UNSUPP)",
            instp0);
    }
    return std::nullopt;
}

std::string ElementKindName(ocsd_gen_trc_elem_t kind) {
    switch (kind) {
        case OCSD_GEN_TRC_ELEM_UNKNOWN: return "Unknown";
        case OCSD_GEN_TRC_ELEM_NO_SYNC: return "NoSync";
        case OCSD_GEN_TRC_ELEM_TRACE_ON: return "TraceOn";
        case OCSD_GEN_TRC_ELEM_EO_TRACE: return "EndOfTrace";
        case OCSD_GEN_TRC_ELEM_PE_CONTEXT: return "PeContext";
        case OCSD_GEN_TRC_ELEM_INSTR_RANGE: return "InstructionRange";
        case OCSD_GEN_TRC_ELEM_I_RANGE_NOPATH: return "InstructionRangeNoPath";
        case OCSD_GEN_TRC_ELEM_ADDR_NACC: return "AddressNotAccessible";
        case OCSD_GEN_TRC_ELEM_ADDR_UNKNOWN: return "AddressUnknown";
        case OCSD_GEN_TRC_ELEM_EXCEPTION: return "Exception";
        case OCSD_GEN_TRC_ELEM_EXCEPTION_RET: return "ExceptionReturn";
        case OCSD_GEN_TRC_ELEM_TIMESTAMP: return "Timestamp";
        case OCSD_GEN_TRC_ELEM_CYCLE_COUNT: return "CycleCount";
        case OCSD_GEN_TRC_ELEM_EVENT: return "Event";
        case OCSD_GEN_TRC_ELEM_SWTRACE: return "SwTrace";
        case OCSD_GEN_TRC_ELEM_SYNC_MARKER: return "SyncMarker";
        case OCSD_GEN_TRC_ELEM_MEMTRANS: return "MemTrans";
        case OCSD_GEN_TRC_ELEM_INSTRUMENTATION: return "Instrumentation";
        case OCSD_GEN_TRC_ELEM_ITMTRACE: return "ItmTrace";
        case OCSD_GEN_TRC_ELEM_CUSTOM: return "Custom";
        default: return std::format("Unrecognized({})", static_cast<int>(kind));
    }
}

std::string RetainedKindName(trace::TraceRecordKind kind) {
    switch (kind) {
        case trace::TraceRecordKind::kInstructionRange: return "InstructionRange";
        case trace::TraceRecordKind::kException: return "Exception";
        case trace::TraceRecordKind::kExceptionReturn: return "ExceptionReturn";
        case trace::TraceRecordKind::kTraceOn: return "TraceOn";
        case trace::TraceRecordKind::kNoSync: return "NoSync";
    }
    return "Unrecognized";
}

std::string GapReasonName(model::GapReason reason) {
    switch (reason) {
        case model::GapReason::kCaptureBoundary: return "captureBoundary";
        case model::GapReason::kTraceOn: return "traceOn";
        case model::GapReason::kOverflow: return "overflow";
        case model::GapReason::kNoSync: return "noSync";
    }
    return "unknown";
}

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

void PrintInstructionText(const model::ReconstructedInstruction &reconstructed, const model::SourceLocation &location) {
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

void PrintGapsText(const std::vector<model::TraceGap> &gaps) {
    std::println();
    std::println("=== gaps ===");
    for (const model::TraceGap &gap : gaps) {
        std::println("  before executed-instruction index {}: {}", gap.instruction_index, GapReasonName(gap.reason));
    }
}

void PrintExceptionsText(const std::vector<model::ExceptionEvent> &events) {
    std::println();
    std::println("=== exceptions ===");
    for (const model::ExceptionEvent &event : events) {
        if (event.is_return) {
            std::println("  before executed-instruction index {}: exception return", event.instruction_index);
        } else {
            std::println("  before executed-instruction index {}: exception entry, number {}",
                          event.instruction_index, event.exception_number);
        }
    }
}

// SVD-aware variant: names each entry via its device's own vector table
// (core exceptions 1-15 architecturally, peripheral IRQs from the SVD).
void PrintExceptionsText(const std::vector<trace::AttributedException> &events) {
    std::println();
    std::println("=== exceptions ===");
    for (const trace::AttributedException &event : events) {
        if (event.is_return) {
            std::println("  before executed-instruction index {}: exception return", event.instruction_index);
        } else {
            std::println("  before executed-instruction index {}: exception entry, number {} ({})",
                          event.instruction_index, event.exception_number, event.name);
        }
    }
}

void PrintFunctionBlocksText(const std::vector<model::FunctionBlock> &functions) {
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

nlohmann::json LocationToJson(const model::SourceLocation &location) {
    nlohmann::json frames = nlohmann::json::array();
    for (const model::InlineFrame &frame : location.frames) {
        frames.push_back({{"function", frame.function}, {"file", frame.file}, {"line", frame.line},
                           {"column", frame.column}});
    }
    return {{"frames", std::move(frames)}};
}

void PrintJson(const std::vector<model::ReconstructedInstruction> &instructions,
               const std::vector<model::FunctionBlock> &function_blocks, const std::vector<model::TraceGap> &gaps,
               const std::vector<trace::TraceRecord> &records, const disasm::ProgramDisassembler &disassembler) {
    nlohmann::json instructions_json = nlohmann::json::array();
    for (const model::ReconstructedInstruction &instruction : instructions) {
        const model::InstructionInfo *info = disassembler.InstructionInfoAt(instruction.insn.address);
        instructions_json.push_back({
            {"address", std::format("0x{:x}", instruction.insn.address)},
            {"bytes", instruction.insn.bytes},
            {"mnemonic", instruction.insn.mnemonic},
            {"operands", instruction.insn.operands},
            {"traceIndex", instruction.trace_index},
            {"traceId", instruction.trace_id},
            {"location", LocationToJson(info != nullptr ? info->location : model::SourceLocation{})},
        });
    }

    nlohmann::json blocks_json = nlohmann::json::array();
    for (const model::FunctionBlock &block : function_blocks) {
        nlohmann::json line_blocks_json = nlohmann::json::array();
        for (const model::LineBlock &line_block : block.line_blocks) {
            line_blocks_json.push_back({
                {"startAddress", std::format("0x{:x}", line_block.start_addr)},
                {"endAddress", std::format("0x{:x}", line_block.end_addr)},
                {"instructionCount", line_block.instr_count},
                {"location", LocationToJson(line_block.location)},
            });
        }
        blocks_json.push_back({
            {"functionName", block.function_name},
            {"entryAddress", std::format("0x{:x}", block.entry_addr)},
            {"lineBlocks", std::move(line_blocks_json)},
        });
    }

    nlohmann::json gaps_json = nlohmann::json::array();
    for (const model::TraceGap &gap : gaps) {
        gaps_json.push_back({{"instructionIndex", gap.instruction_index}, {"reason", GapReasonName(gap.reason)}});
    }

    nlohmann::json records_json = nlohmann::json::array();
    for (const trace::TraceRecord &record : records) {
        nlohmann::json entry = {
            {"traceIndex", record.index_sop},
            {"traceId", record.trace_id},
            {"kind", RetainedKindName(record.kind)},
        };
        if (record.kind == trace::TraceRecordKind::kInstructionRange) {
            entry["startAddress"] = std::format("0x{:x}", record.start_addr);
            entry["endAddress"] = std::format("0x{:x}", record.end_addr);
        } else if (record.kind == trace::TraceRecordKind::kException) {
            entry["exceptionNumber"] = record.exception_number;
        }
        if (record.cycle_count) entry["cycleCount"] = *record.cycle_count;
        if (record.timestamp) entry["timestamp"] = *record.timestamp;
        if (record.timestamp_cycle_count) entry["timestampCycleCount"] = *record.timestamp_cycle_count;
        records_json.push_back(std::move(entry));
    }

    nlohmann::json root = {
        {"instructions", std::move(instructions_json)},
        {"functionBlocks", std::move(blocks_json)},
        {"gaps", std::move(gaps_json)},
        {"records", std::move(records_json)},
    };
    std::println("{}", root.dump(2));
}

void PrintElementCoverage(const std::vector<trace::TraceRecord> &records,
                           const std::map<ocsd_gen_trc_elem_t, uint64_t> &dropped) {
    std::map<trace::TraceRecordKind, uint64_t> retained;
    for (const trace::TraceRecord &record : records) {
        retained[record.kind]++;
    }

    std::println(stderr, "trailer-trace-replay: element coverage");
    std::println(stderr, "  retained:");
    for (const auto &[kind, count] : retained) {
        std::println(stderr, "    {}: {}", RetainedKindName(kind), count);
    }
    std::println(stderr, "  dropped:");
    if (dropped.empty()) {
        std::println(stderr, "    (none)");
    }
    for (const auto &[kind, count] : dropped) {
        std::println(stderr, "    {}: {}", ElementKindName(kind), count);
    }

    uint64_t with_cycle_count = 0;
    uint64_t with_timestamp = 0;
    std::map<unsync_info_t, uint64_t> no_sync_reasons;
    std::map<trace_on_reason_t, uint64_t> trace_on_reasons;
    for (const trace::TraceRecord &record : records) {
        if (record.cycle_count) ++with_cycle_count;
        if (record.timestamp) ++with_timestamp;
        if (record.kind == trace::TraceRecordKind::kNoSync) no_sync_reasons[record.no_sync_reason]++;
        if (record.kind == trace::TraceRecordKind::kTraceOn) trace_on_reasons[record.trace_on_reason]++;
    }
    std::println(stderr, "  retained records carrying a cycle count: {}", with_cycle_count);
    std::println(stderr, "  retained records carrying a timestamp: {}", with_timestamp);
    std::println(stderr, "  NoSync raw reasons:");
    for (const auto &[reason, count] : no_sync_reasons) {
        std::println(stderr, "    {}: {}", static_cast<int>(reason), count);
    }
    std::println(stderr, "  TraceOn raw reasons:");
    for (const auto &[reason, count] : trace_on_reasons) {
        std::println(stderr, "    {}: {}", static_cast<int>(reason), count);
    }
}

}  // namespace

int main(int argc, char **argv) {
    const std::optional<Args> parsed = ParseArgs(argc, argv);
    if (!parsed) {
        return EXIT_FAILURE;
    }
    const Args &args = *parsed;

    const std::expected<std::vector<uint8_t>, std::string> trace_data = ReadFile(args.capture_path);
    if (!trace_data) {
        return Fail(trace_data.error());
    }

    const model::Etmv4Registers regs = args.etm_regs_path
                                            ? [&]() -> model::Etmv4Registers {
                                                  auto loaded = LoadEtmv4Registers(*args.etm_regs_path);
                                                  if (!loaded) {
                                                      std::println(stderr, "trailer-trace-replay: {}", loaded.error());
                                                      std::exit(EXIT_FAILURE);
                                                  }
                                                  return *loaded;
                                              }()
                                            : DefaultEtmv4Registers();

    if (const std::optional<std::string> limit_error = CheckConfigLimits(regs)) {
        return Fail(*limit_error);
    }

    model::DeformatterConfig deformatter;
    deformatter.source_format = model::TraceSourceFormat::kFrameFormatted;
    deformatter.frame_sync = model::FrameSyncMode::kMemAligned;
    deformatter.reset_on_4x_fsync = true;

    model::InstructionTraceDecodeConfig::Builder builder;
    builder.SetProgramPath(args.elf_path).SetCoreName("Cortex-M7").SetDeformatter(deformatter).SetRegisters(regs);
    std::vector<uint8_t> trace_data_copy = *trace_data;
    builder.SetTraceData(std::move(trace_data_copy));
    const model::InstructionTraceDecodeConfig config = std::move(builder).Build();

    const Clock::time_point precompute_start = Clock::now();
    std::expected<disasm::ProgramDisassembler, std::string> disasm_result =
        disasm::ProgramDisassembler::Create(config.program_path());
    if (!disasm_result) {
        return Fail(disasm_result.error());
    }
    disasm::ProgramDisassembler &disassembler = *disasm_result;
    const double precompute_ms = ElapsedMs(precompute_start, Clock::now());

    decode::TraceDecoder decoder;
    std::map<ocsd_gen_trc_elem_t, uint64_t> dropped_elements;
    const Clock::time_point decode_start = Clock::now();
    const std::expected<std::vector<trace::TraceRecord>, std::string> decode_result =
        decoder.Decode(config, disassembler.load_segments(), &dropped_elements);
    const double decode_ms = ElapsedMs(decode_start, Clock::now());
    if (!decode_result) {
        return Fail(decode_result.error());
    }
    const std::vector<trace::TraceRecord> &records = *decode_result;

    if (args.dump_elements) {
        PrintElementCoverage(records, dropped_elements);
    }

    uint64_t resolve_calls = 0;
    auto resolve = [&](uint64_t address) {
        ++resolve_calls;
        return disassembler.InstructionInfoAt(address);
    };

    const Clock::time_point instructions_start = Clock::now();
    const std::vector<model::ReconstructedInstruction> all_instructions =
        xform::Transform<model::ReconstructedInstruction>(records, resolve);
    const double transform_instructions_ms = ElapsedMs(instructions_start, Clock::now());

    const Clock::time_point blocks_start = Clock::now();
    const std::vector<model::FunctionBlock> function_blocks = xform::TraceTransform<model::FunctionBlock>::Apply(
        std::span<const model::ReconstructedInstruction>(all_instructions), resolve);
    const double transform_blocks_ms = ElapsedMs(blocks_start, Clock::now());

    const Clock::time_point gaps_start = Clock::now();
    const std::vector<model::TraceGap> gaps = xform::Transform<model::TraceGap>(records);
    const double transform_gaps_ms = ElapsedMs(gaps_start, Clock::now());

    const std::vector<model::ExceptionEvent> exceptions = xform::Transform<model::ExceptionEvent>(records);

    std::optional<device_xml::Device> device;
    if (args.svd_path) {
        std::expected<device_xml::Device, std::string> loaded = device_xml::LoadSvd(*args.svd_path);
        if (!loaded) {
            return Fail(std::format("failed to load SVD '{}': {}", args.svd_path->string(), loaded.error()));
        }
        device = std::move(*loaded);
    }

    if (args.format == "json") {
        PrintJson(all_instructions, function_blocks, gaps, records, disassembler);
    } else {
        for (const model::ReconstructedInstruction &instruction : all_instructions) {
            const model::InstructionInfo *info = resolve(instruction.insn.address);
            PrintInstructionText(instruction, info != nullptr ? info->location : model::SourceLocation{});
        }
        PrintFunctionBlocksText(function_blocks);
        PrintGapsText(gaps);
        if (device) {
            PrintExceptionsText(trace::AttributeExceptions(exceptions, *device));
        } else {
            PrintExceptionsText(exceptions);
        }
    }

    std::println(stderr, "trailer-trace-replay: {} trace records, {} reconstructed instructions", records.size(),
                  all_instructions.size());

    if (args.metrics_path) {
        nlohmann::json elements_by_kind = nlohmann::json::object();
        std::map<trace::TraceRecordKind, uint64_t> retained;
        for (const trace::TraceRecord &record : records) {
            retained[record.kind]++;
        }
        for (const auto &[kind, count] : retained) {
            elements_by_kind[RetainedKindName(kind)] = count;
        }
        for (const auto &[kind, count] : dropped_elements) {
            elements_by_kind[ElementKindName(kind)] = count;
        }

        nlohmann::json gaps_by_reason = nlohmann::json::object();
        for (const model::TraceGap &gap : gaps) {
            const std::string name = GapReasonName(gap.reason);
            gaps_by_reason[name] = gaps_by_reason.value(name, uint64_t{0}) + 1;
        }

        const nlohmann::json metrics = {
            {"bytesIn", trace_data->size()},
            {"precomputeMs", precompute_ms},
            {"decodeMs", decode_ms},
            {"transformInstructionsMs", transform_instructions_ms},
            {"transformBlocksMs", transform_blocks_ms},
            {"transformGapsMs", transform_gaps_ms},
            {"traceRecords", records.size()},
            {"instructionsReconstructed", all_instructions.size()},
            {"functionBlocks", function_blocks.size()},
            {"gaps", gaps.size()},
            {"gapsByReason", std::move(gaps_by_reason)},
            {"elementsByKind", std::move(elements_by_kind)},
            {"resolveCalls", resolve_calls},
        };

        std::ofstream metrics_file(*args.metrics_path);
        if (!metrics_file) {
            return Fail(std::format("failed to open metrics output '{}'", args.metrics_path->string()));
        }
        metrics_file << metrics.dump(2);
    }

    return EXIT_SUCCESS;
}
