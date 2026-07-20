#ifndef TRAILER_CONFIG_PARSER_PIPELINE_CONFIG_HPP_
#define TRAILER_CONFIG_PARSER_PIPELINE_CONFIG_HPP_

#include <cstdint>
#include <string>

namespace config {

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
    uint32_t trcconfigr;
    uint32_t trctraceidr;
    uint32_t trcidr0;
    uint32_t trcidr1;
    uint32_t trcidr2;
    uint32_t trcidr8;
    uint32_t trcidr9;
    uint32_t trcidr10;
    uint32_t trcidr11;
    uint32_t trcidr12;
    uint32_t trcidr13;

    // Captured for future analysis; ocsd_etmv4_cfg has no matching fields.
    uint32_t trcidr3;
    uint32_t trcidr4;
    uint32_t trcidr5;
    uint32_t trcidr6;
    uint32_t trcidr7;
    uint32_t trcauthstatus;
};

// Output of the configuration-loading pipeline stage: everything later
// stages need to locate the trace/program inputs and build a decoder.
struct PipelineConfig {
    std::string trace_dump_path;
    std::string program_path;
    // CoreSight core name (e.g. "Cortex-A53"), used to resolve the ETMv4
    // architecture version and core profile.
    std::string core_name;
    DeformatterConfig deformatter;
    Etmv4Registers regs;
};

}  // namespace config

#endif  // TRAILER_CONFIG_PARSER_PIPELINE_CONFIG_HPP_
