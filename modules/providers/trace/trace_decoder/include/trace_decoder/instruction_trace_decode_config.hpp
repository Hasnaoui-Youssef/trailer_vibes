#ifndef TRAILER_TRACE_DECODER_INSTRUCTION_TRACE_DECODE_CONFIG_HPP_
#define TRAILER_TRACE_DECODER_INSTRUCTION_TRACE_DECODE_CONFIG_HPP_

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace decode {

// How the trace byte stream is demultiplexed by the CoreSight trace frame
// deformatter before reaching the protocol decoder.
enum class TraceSourceFormat : uint8_t {
    kFrameFormatted,  // Multiplexed CoreSight trace frames (OCSD_TRC_SRC_FRAME_FORMATTED).
    kSingle,          // Single trace source, no frame demux (OCSD_TRC_SRC_SINGLE).
};

// Byte alignment / synchronization scheme used by the frame deformatter.
enum class FrameSyncMode : uint8_t {
    kMemAligned,  // 16-byte memory-aligned frames, no sync bytes (OCSD_DFRMTR_FRAME_MEM_ALIGN).
    kFsync,       // Frame syncs present, 4-byte aligned (OCSD_DFRMTR_HAS_FSYNCS).
    kHsync,       // Half-word syncs present, 2-byte aligned (OCSD_DFRMTR_HAS_HSYNCS).
};

// Configuration for the CoreSight trace frame deformatter / demultiplexer
// stage that precedes protocol decode. Mirrors the arguments required to
// build an OpenCSD DecodeTree (DecodeTree::CreateDecodeTree).
struct DeformatterConfig {
    TraceSourceFormat source_format = TraceSourceFormat::kFrameFormatted;
    FrameSyncMode frame_sync = FrameSyncMode::kMemAligned;
    // Reset downstream decoders on 4 consecutive frame-aligned fsyncs
    // (OCSD_DFRMTR_RESET_ON_4X_FSYNC).
    bool reset_on_4x_fsync = false;
};

// ETMv4 trace ID and configuration registers required to build an OpenCSD
// ETMv4 decoder. Values are read verbatim from the target and interpreted
// as 32-bit unsigned integers.
struct Etmv4Registers {
    // Consumed directly by ocsd_etmv4_cfg.
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

    // Captured for future analysis; ocsd_etmv4_cfg has no matching fields.
    uint32_t trcidr3 = 0;
    uint32_t trcidr4 = 0;
    uint32_t trcidr5 = 0;
    uint32_t trcidr6 = 0;
    uint32_t trcidr7 = 0;
    uint32_t trcauthstatus = 0;
};

// Everything TraceDecoder::Decode needs to run: where the program image
// lives, which CoreSight core it targets, how its trace stream is framed,
// its ETMv4 configuration registers, and the trace bytes themselves.
//
// There is no parsing step at this level - whoever assembles the pipeline
// (the DAP layer for program_path, an OpenOCD TCL client for regs/
// deformatter/trace_data) constructs this directly via Builder from data
// it already holds in memory. This type never implies a serialization
// format or a transport.
//
// program_path remains a filesystem path regardless of producer: OpenCSD's
// memory accessor reads the program image's opcode bytes from it by file
// offset (see trace_decoder.cc), it is not read into memory by this stage.
// trace_data, by contrast, is genuinely in-memory: it is fed directly into
// the decode tree with no filesystem step of its own.
class InstructionTraceDecodeConfig {
public:
    // Defined below, once InstructionTraceDecodeConfig is a complete type -
    // Builder holds one by value, so it cannot be defined inline here.
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

}  // namespace decode

#endif  // TRAILER_TRACE_DECODER_INSTRUCTION_TRACE_DECODE_CONFIG_HPP_
