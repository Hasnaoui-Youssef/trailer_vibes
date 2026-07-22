#include "config_parser/config_parser.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "config_parser_detail.hpp"

namespace config {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr std::string_view kTraceDumpKey = "trace_dump";
constexpr std::string_view kProgKey = "prog";
constexpr std::string_view kCoreKey = "core";
constexpr std::string_view kTraceFormatKey = "trace_format";
constexpr std::string_view kRegsKey = "regs";

constexpr std::string_view kTraceFormatSourceKey = "source";
constexpr std::string_view kTraceFormatFrameSyncKey = "frame_sync";
constexpr std::string_view kTraceFormatResetOn4xFsyncKey = "reset_on_4x_fsync";

constexpr std::string_view kSourceFrame = "frame";
constexpr std::string_view kSourceSingle = "single";

constexpr std::string_view kFrameSyncMemAligned = "mem_aligned";
constexpr std::string_view kFrameSyncFsync = "fsync";
constexpr std::string_view kFrameSyncHsync = "hsync";

// Registers consumed directly by ocsd_etmv4_cfg; the schema requires these.
constexpr std::array<std::pair<std::string_view, uint32_t Etmv4Registers::*>, 11> kRequiredRegisterFields{{
    {"TRCCONFIGR", &Etmv4Registers::trcconfigr},
    {"TRCTRACEIDR", &Etmv4Registers::trctraceidr},
    {"TRCIDR0", &Etmv4Registers::trcidr0},
    {"TRCIDR1", &Etmv4Registers::trcidr1},
    {"TRCIDR2", &Etmv4Registers::trcidr2},
    {"TRCIDR8", &Etmv4Registers::trcidr8},
    {"TRCIDR9", &Etmv4Registers::trcidr9},
    {"TRCIDR10", &Etmv4Registers::trcidr10},
    {"TRCIDR11", &Etmv4Registers::trcidr11},
    {"TRCIDR12", &Etmv4Registers::trcidr12},
    {"TRCIDR13", &Etmv4Registers::trcidr13},
}};

// Registers captured for future analysis; ocsd_etmv4_cfg has no matching
// field for these, so they default to 0 when absent from the input.
constexpr std::array<std::pair<std::string_view, uint32_t Etmv4Registers::*>, 6> kOptionalRegisterFields{{
    {"TRCIDR3", &Etmv4Registers::trcidr3},
    {"TRCIDR4", &Etmv4Registers::trcidr4},
    {"TRCIDR5", &Etmv4Registers::trcidr5},
    {"TRCIDR6", &Etmv4Registers::trcidr6},
    {"TRCIDR7", &Etmv4Registers::trcidr7},
    {"TRCAUTHSTATUS", &Etmv4Registers::trcauthstatus},
}};

bool FitsUint32(const Json &value) {
    if (!value.is_number_integer()) {
        return false;
    }
    if (value.is_number_unsigned()) {
        return value.get<uint64_t>() <= std::numeric_limits<uint32_t>::max();
    }
    const int64_t v = value.get<int64_t>();
    return v >= 0 && v <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max());
}

}  // namespace

std::expected<PipelineConfig, std::string> ConfigParser::ParseJson(std::string_view path) const {
    const std::string path_str(path);

    std::ifstream file(path_str);
    if (!file.is_open()) {
        return std::unexpected(detail::MakeError(path, "failed to open configuration file"));
    }

    Json root = Json::parse(file, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        return std::unexpected(detail::MakeError(path, "not valid JSON"));
    }
    if (!root.is_object()) {
        return std::unexpected(detail::MakeError(path, "root must be a JSON object"));
    }

    for (const auto &[key, value] : root.items()) {
        if (key != kTraceDumpKey && key != kProgKey && key != kCoreKey && key != kTraceFormatKey &&
            key != kRegsKey) {
            return std::unexpected(detail::MakeError(path, "unexpected top-level field '" + key + "'"));
        }
    }

    PipelineConfig config;

    // Relative trace_dump / prog paths are resolved against the directory
    // containing the config file itself (not the process's cwd), so the
    // config remains portable regardless of where the engine is launched
    // from. fs::path's operator/ leaves an already-absolute RHS untouched,
    // so an absolute value in the config passes through as-is.
    const fs::path config_dir = fs::absolute(fs::path(path_str)).parent_path();

    if (!root.contains(kTraceDumpKey) || !root[std::string(kTraceDumpKey)].is_string() ||
        root[std::string(kTraceDumpKey)].get<std::string>().empty()) {
        return std::unexpected(detail::MakeError(path, "'trace_dump' must be a non-empty string"));
    }
    config.trace_dump_path = config_dir / root[std::string(kTraceDumpKey)].get<std::string>();

    if (!root.contains(kProgKey) || !root[std::string(kProgKey)].is_string() ||
        root[std::string(kProgKey)].get<std::string>().empty()) {
        return std::unexpected(detail::MakeError(path, "'prog' must be a non-empty string"));
    }
    config.program_path = config_dir / root[std::string(kProgKey)].get<std::string>();

    if (!root.contains(kCoreKey) || !root[std::string(kCoreKey)].is_string() ||
        root[std::string(kCoreKey)].get<std::string>().empty()) {
        return std::unexpected(detail::MakeError(path, "'core' must be a non-empty string"));
    }
    config.core_name = root[std::string(kCoreKey)].get<std::string>();

    if (!root.contains(kTraceFormatKey) || !root[std::string(kTraceFormatKey)].is_object()) {
        return std::unexpected(detail::MakeError(path, "'trace_format' must be an object"));
    }
    const Json &trace_format = root[std::string(kTraceFormatKey)];

    for (const auto &[key, value] : trace_format.items()) {
        if (key != kTraceFormatSourceKey && key != kTraceFormatFrameSyncKey && key != kTraceFormatResetOn4xFsyncKey) {
            return std::unexpected(detail::MakeError(path, "unexpected field 'trace_format." + key + "'"));
        }
    }

    if (!trace_format.contains(kTraceFormatSourceKey) ||
        !trace_format[std::string(kTraceFormatSourceKey)].is_string()) {
        return std::unexpected(detail::MakeError(path, "'trace_format.source' must be a string"));
    }
    const std::string source = trace_format[std::string(kTraceFormatSourceKey)].get<std::string>();
    if (source == kSourceFrame) {
        config.deformatter.source_format = TraceSourceFormat::kFrameFormatted;
    } else if (source == kSourceSingle) {
        config.deformatter.source_format = TraceSourceFormat::kSingle;
    } else {
        return std::unexpected(detail::MakeError(path, "'trace_format.source' must be 'frame' or 'single'"));
    }

    config.deformatter.frame_sync = FrameSyncMode::kMemAligned;
    if (trace_format.contains(kTraceFormatFrameSyncKey)) {
        if (!trace_format[std::string(kTraceFormatFrameSyncKey)].is_string()) {
            return std::unexpected(detail::MakeError(path, "'trace_format.frame_sync' must be a string"));
        }
        const std::string frame_sync = trace_format[std::string(kTraceFormatFrameSyncKey)].get<std::string>();
        if (frame_sync == kFrameSyncMemAligned) {
            config.deformatter.frame_sync = FrameSyncMode::kMemAligned;
        } else if (frame_sync == kFrameSyncFsync) {
            config.deformatter.frame_sync = FrameSyncMode::kFsync;
        } else if (frame_sync == kFrameSyncHsync) {
            config.deformatter.frame_sync = FrameSyncMode::kHsync;
        } else {
            return std::unexpected(
                detail::MakeError(path, "'trace_format.frame_sync' must be 'mem_aligned', 'fsync' or 'hsync'"));
        }
    }

    config.deformatter.reset_on_4x_fsync = false;
    if (trace_format.contains(kTraceFormatResetOn4xFsyncKey)) {
        if (!trace_format[std::string(kTraceFormatResetOn4xFsyncKey)].is_boolean()) {
            return std::unexpected(detail::MakeError(path, "'trace_format.reset_on_4x_fsync' must be a boolean"));
        }
        config.deformatter.reset_on_4x_fsync = trace_format[std::string(kTraceFormatResetOn4xFsyncKey)].get<bool>();
    }

    if (!root.contains(kRegsKey) || !root[std::string(kRegsKey)].is_object()) {
        return std::unexpected(detail::MakeError(path, "'regs' must be an object"));
    }
    const Json &regs = root[std::string(kRegsKey)];

    for (const auto &[reg_name, member] : kRequiredRegisterFields) {
        if (!regs.contains(reg_name)) {
            return std::unexpected(detail::MakeError(path, "'regs' is missing register '" + std::string(reg_name) + "'"));
        }
        const Json &value = regs[std::string(reg_name)];
        if (!FitsUint32(value)) {
            return std::unexpected(
                detail::MakeError(path, "'regs." + std::string(reg_name) + "' must be a 32-bit unsigned integer"));
        }
        config.regs.*member = value.get<uint32_t>();
    }

    for (const auto &[reg_name, member] : kOptionalRegisterFields) {
        if (!regs.contains(reg_name)) {
            config.regs.*member = 0;
            continue;
        }
        const Json &value = regs[std::string(reg_name)];
        if (!FitsUint32(value)) {
            return std::unexpected(
                detail::MakeError(path, "'regs." + std::string(reg_name) + "' must be a 32-bit unsigned integer"));
        }
        config.regs.*member = value.get<uint32_t>();
    }

    return config;
}

}  // namespace config
