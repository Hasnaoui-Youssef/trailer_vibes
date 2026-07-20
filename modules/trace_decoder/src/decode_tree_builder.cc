#include "trace_decoder/decode_tree_builder.hpp"

#include <cstdint>
#include <utility>

namespace decode {

namespace {

using config::DeformatterConfig;
using config::FrameSyncMode;
using config::TraceSourceFormat;

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

ocsd_etmv4_cfg ToEtmv4Config(const config::Etmv4Registers &regs, ocsd_arch_version_t arch_ver,
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

}  // namespace

BuildResult DecodeTreeBuilder::Build(const config::PipelineConfig &config, trace::TraceRecordSink &sink) const {
    BuildResult result;

    CoreArchProfileMap arch_profiles;
    const ocsd_arch_profile_t arch_profile = arch_profiles.getArchProfile(config.core_name);
    if (arch_profile.arch == ARCH_UNKNOWN) {
        result.error = MakeError("unrecognized core name '" + config.core_name + "'");
        return result;
    }

    const ocsd_dcd_tree_src_t src_format = ToSourceFormat(config.deformatter.source_format);
    const uint32_t formatter_flags = ToFormatterFlags(config.deformatter);

    DecodeTreePtr tree(DecodeTree::CreateDecodeTree(src_format, formatter_flags));
    if (tree == nullptr) {
        result.error = MakeError("failed to create decode tree");
        return result;
    }

    if (tree->createMemAccMapper() != OCSD_OK) {
        result.error = MakeError("failed to create memory access mapper");
        return result;
    }

    const ocsd_etmv4_cfg etmv4_cfg = ToEtmv4Config(config.regs, arch_profile.arch, arch_profile.profile);
    EtmV4Config config_obj(&etmv4_cfg);

    const ocsd_err_t err = tree->createDecoder(OCSD_BUILTIN_DCD_ETMV4I, OCSD_CREATE_FLG_FULL_DECODER, &config_obj);
    if (err != OCSD_OK) {
        result.error = MakeError("failed to create ETMv4 decoder");
        return result;
    }

    tree->setGenTraceElemOutI(&sink);

    result.tree = std::move(tree);
    return result;
}

}  // namespace decode
