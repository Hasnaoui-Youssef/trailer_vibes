#include "trace_decoder/trace_decoder.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <utility>

#include <opencsd.h>

#include "trace_sink/trace_record_sink.hpp"

namespace decode {

namespace {

// Frees a DecodeTree created via DecodeTree::CreateDecodeTree. Private to
// this translation unit: DecodeTree never crosses TraceDecoder's public
// interface.
struct DecodeTreeDeleter {
    void operator()(DecodeTree *tree) const {
        if (tree != nullptr) {
            DecodeTree::DestroyDecodeTree(tree);
        }
    }
};
using DecodeTreePtr = std::unique_ptr<DecodeTree, DecodeTreeDeleter>;

ocsd_dcd_tree_src_t ToSourceFormat(TraceSourceFormat format) {
    switch (format) {
        case TraceSourceFormat::kSingle:
            return OCSD_TRC_SRC_SINGLE;
        case TraceSourceFormat::kFrameFormatted:
            return OCSD_TRC_SRC_FRAME_FORMATTED;
    }
    return OCSD_TRC_SRC_FRAME_FORMATTED;
}

uint32_t ToFormatterFlags(const DeformatterConfig &deformatter) {
    uint32_t flags = 0;
    switch (deformatter.frame_sync) {
        case FrameSyncMode::kFsync:
            flags |= OCSD_DFRMTR_HAS_FSYNCS;
            break;
        case FrameSyncMode::kHsync:
            flags |= OCSD_DFRMTR_HAS_HSYNCS;
            break;
        case FrameSyncMode::kMemAligned:
            flags |= OCSD_DFRMTR_FRAME_MEM_ALIGN;
            break;
    }
    if (deformatter.reset_on_4x_fsync) {
        flags |= OCSD_DFRMTR_RESET_ON_4X_FSYNC;
    }
    return flags;
}

ocsd_etmv4_cfg ToEtmv4Config(const Etmv4Registers &regs, ocsd_arch_version_t arch_ver,
                              ocsd_core_profile_t core_prof) {
    ocsd_etmv4_cfg cfg{};
    cfg.reg_configr = regs.trcconfigr;
    cfg.reg_traceidr = regs.trctraceidr;
    cfg.reg_idr0 = regs.trcidr0;
    cfg.reg_idr1 = regs.trcidr1;
    cfg.reg_idr2 = regs.trcidr2;
    cfg.reg_idr8 = regs.trcidr8;
    cfg.reg_idr9 = regs.trcidr9;
    cfg.reg_idr10 = regs.trcidr10;
    cfg.reg_idr11 = regs.trcidr11;
    cfg.reg_idr12 = regs.trcidr12;
    cfg.reg_idr13 = regs.trcidr13;
    cfg.arch_ver = arch_ver;
    cfg.core_prof = core_prof;
    return cfg;
}

std::string MakeError(std::string_view message) { return std::string("trace_decoder: ").append(message); }

// Registers `segments` as file-backed opcode memory for `tree`, reading
// bytes from `image_path` (the same ELF disasm::ProgramDisassembler loaded
// them from). Mirrors snapshot_parser's CreateDcdTreeFromSnapShot::
// processDumpfiles, just driven by PT_LOAD segments instead of Arm
// Debug/Trace snapshot dump-file entries.
//
// A firmware image typically has more than one PT_LOAD segment (e.g. one
// for .text, another for .data's flash-resident initial values), and
// OpenCSD only allows *one* accessor to be created per file path: the
// first region for a given file must go through addBinFileRegionMemAcc,
// every subsequent region for that same file through
// updateBinFileRegionMemAcc, or the add call fails outright. Hence the
// isExistingFileAccessor check on every iteration, exactly as
// processDumpfiles does.
ocsd_err_t RegisterMemoryImage(DecodeTree &tree, std::span<const model::LoadSegment> segments,
                               const std::string &image_path) {
    for (const model::LoadSegment &segment : segments) {
        ocsd_file_mem_region_t region{};
        region.file_offset = static_cast<size_t>(segment.file_offset);
        region.start_address = static_cast<ocsd_vaddr_t>(segment.vaddr);
        region.region_size = static_cast<size_t>(segment.size);

        const ocsd_err_t err = TrcMemAccessorFile::isExistingFileAccessor(image_path)
                                    ? tree.updateBinFileRegionMemAcc(&region, 1, OCSD_MEM_SPACE_ANY, image_path)
                                    : tree.addBinFileRegionMemAcc(&region, 1, OCSD_MEM_SPACE_ANY, image_path);
        if (err != OCSD_OK) {
            return err;
        }
    }
    return OCSD_OK;
}

// Feeds `data` through `tree` in fixed-size chunks (mirrors OpenCSD's own
// mem_buff_demo sample), then signals end-of-trace so buffered elements
// flush out. Returns the final datapath response.
ocsd_datapath_resp_t FeedTraceData(DecodeTree &tree, std::span<const uint8_t> data) {
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
            std::cerr << "trace_decoder: decoder stalled at trace index " << index << " without consuming data\n";
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

}  // namespace

std::expected<std::vector<trace::TraceRecord>, std::string> TraceDecoder::Decode(
    const InstructionTraceDecodeConfig &config, std::span<const model::LoadSegment> segments) const {
    CoreArchProfileMap arch_profiles;
    const ocsd_arch_profile_t arch_profile = arch_profiles.getArchProfile(config.core_name());
    if (arch_profile.arch == ARCH_UNKNOWN) {
        return std::unexpected(MakeError("unrecognized core name '" + config.core_name() + "'"));
    }

    const ocsd_dcd_tree_src_t src_format = ToSourceFormat(config.deformatter().source_format);
    const uint32_t formatter_flags = ToFormatterFlags(config.deformatter());

    DecodeTreePtr tree(DecodeTree::CreateDecodeTree(src_format, formatter_flags));
    if (tree == nullptr) {
        return std::unexpected(MakeError("failed to create decode tree"));
    }

    if (tree->createMemAccMapper() != OCSD_OK) {
        return std::unexpected(MakeError("failed to create memory access mapper"));
    }

    const std::string program_path = config.program_path().string();
    if (RegisterMemoryImage(*tree, segments, program_path) != OCSD_OK) {
        return std::unexpected(MakeError("failed to register program image '" + program_path + "' as decoder memory"));
    }

    const ocsd_etmv4_cfg etmv4_cfg = ToEtmv4Config(config.registers(), arch_profile.arch, arch_profile.profile);
    EtmV4Config config_obj(&etmv4_cfg);

    if (tree->createDecoder(OCSD_BUILTIN_DCD_ETMV4I, OCSD_CREATE_FLG_FULL_DECODER, &config_obj) != OCSD_OK) {
        return std::unexpected(MakeError("failed to create ETMv4 decoder"));
    }

    trace::TraceRecordSink sink;
    tree->setGenTraceElemOutI(&sink);

    const ocsd_datapath_resp_t decode_resp = FeedTraceData(*tree, config.trace_data());
    if (OCSD_DATA_RESP_IS_FATAL(decode_resp)) {
        return std::unexpected(
            MakeError("decode failed with fatal datapath response " + std::to_string(static_cast<int>(decode_resp))));
    }
    if (OCSD_DATA_RESP_IS_WARN_OR_ERR(decode_resp)) {
        std::cerr << "trace_decoder: decode completed with warnings/errors (datapath response "
                   << static_cast<int>(decode_resp) << ")\n";
    }

    return sink.TakeRecords();
}

}  // namespace decode
