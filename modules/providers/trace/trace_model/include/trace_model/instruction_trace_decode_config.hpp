#ifndef TRAILER_TRACE_MODEL_INSTRUCTION_TRACE_DECODE_CONFIG_HPP_
#define TRAILER_TRACE_MODEL_INSTRUCTION_TRACE_DECODE_CONFIG_HPP_

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace model {

enum class TraceSourceFormat : uint8_t {
    kFrameFormatted,  // Multiplexed CoreSight trace frames (OCSD_TRC_SRC_FRAME_FORMATTED).
    kSingle,          // Single trace source, no frame demux (OCSD_TRC_SRC_SINGLE).
};

enum class FrameSyncMode : uint8_t {
    kMemAligned,  // 16-byte memory-aligned frames.
    kFsync,       // 4-byte aligned Frame syncs present.
    kHsync,       // Half-syncs present,
};
struct DeformatterConfig { TraceSourceFormat source_format = TraceSourceFormat::kFrameFormatted;
    FrameSyncMode frame_sync = FrameSyncMode::kMemAligned;
    // (OCSD_DFRMTR_RESET_ON_4X_FSYNC).
    bool reset_on_4x_fsync = false;
};

struct Etmv4Registers {
    uint32_t trcconfigr = 0;
    uint32_t trctraceidr = 0;
    uint32_t trcidr0 = 0;
    uint32_t trcidr1 = 0;
    uint32_t trcidr2 = 0;
    uint32_t trcidr8 = 0;
    uint32_t trcidr9 = 0;
    uint32_t trcidr10 = 0;
    uint32_t trcidr11 = 0;
    uint32_t trcidr12 = 0;
    uint32_t trcidr13 = 0;
    uint32_t trcidr3 = 0;
    uint32_t trcidr4 = 0;
    uint32_t trcidr5 = 0;
    uint32_t trcidr6 = 0;
    uint32_t trcidr7 = 0;
    uint32_t trcauthstatus = 0;
};

// Everything TraceDecoder::Decode needs to run
class InstructionTraceDecodeConfig {
public:
    class Builder;

    const std::filesystem::path &program_path() const { return program_path_; }
    const std::string &core_name() const { return core_name_; }
    const DeformatterConfig &deformatter() const { return deformatter_; }
    const Etmv4Registers &registers() const { return regs_; }
    const std::vector<uint8_t> &trace_data() const { return trace_data_; }

private:
    InstructionTraceDecodeConfig() = default;

    std::filesystem::path program_path_;
    std::string core_name_;
    DeformatterConfig deformatter_;
    Etmv4Registers regs_;
    std::vector<uint8_t> trace_data_;
};

class InstructionTraceDecodeConfig::Builder {
public:
    Builder &SetProgramPath(std::filesystem::path program_path) {
        config_.program_path_ = std::move(program_path);
        return *this;
    }
    Builder &SetCoreName(std::string core_name) {
        config_.core_name_ = std::move(core_name);
        return *this;
    }
    Builder &SetDeformatter(DeformatterConfig deformatter) {
        config_.deformatter_ = deformatter;
        return *this;
    }
    Builder &SetRegisters(Etmv4Registers regs) {
        config_.regs_ = regs;
        return *this;
    }
    Builder &SetTraceData(std::vector<uint8_t> trace_data) {
        config_.trace_data_ = std::move(trace_data);
        return *this;
    }

    InstructionTraceDecodeConfig Build() && { return std::move(config_); }

private:
    InstructionTraceDecodeConfig config_;
};

}  // namespace model

#endif  // TRAILER_TRACE_MODEL_INSTRUCTION_TRACE_DECODE_CONFIG_HPP_
