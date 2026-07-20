#ifndef TRAILER_CONFIG_PARSER_CONFIG_PARSER_HPP_
#define TRAILER_CONFIG_PARSER_CONFIG_PARSER_HPP_

#include <optional>
#include <string>
#include <string_view>

#include "config_parser/pipeline_config.hpp"

namespace config {

// Result of a configuration parse attempt. On failure `config` is empty and
// `error` describes what went wrong (offending field, expected type/value)
// with enough context for an external caller to react to.
struct ParseResult {
    std::optional<PipelineConfig> config;
    std::string error;

    bool Ok() const { return config.has_value(); }
};

// Configuration-loading pipeline stage. Loads and validates pipeline
// configuration from external, transport-specific sources and converts it
// into a PipelineConfig for the rest of the pipeline to consume. Each
// supported input format gets its own Parse* entry point so new formats
// (e.g. Arm Debug and Trace snapshots) can be added without disturbing
// existing ones.
class ConfigParser {
public:
    // Parses `path` as a JSON configuration file. Expected schema:
    //
    //   {
    //     "trace_dump": "<path to trace dump file>",
    //     "prog": "<path to binary/hex/elf image>",
    //     "core": "<CoreSight core name, e.g. Cortex-A53>",
    //     "trace_format": {
    //       "source": "frame" | "single",
    //       "frame_sync": "mem_aligned" | "fsync" | "hsync",  // optional, default "mem_aligned"
    //       "reset_on_4x_fsync": <bool>                       // optional, default false
    //     },
    //     "regs": { "<ETMv4 register name>": <uint32>, ... }
    //   }
    //
    // `regs` must supply at least TRCCONFIGR, TRCTRACEIDR, TRCIDR0-2 and
    // TRCIDR8-13 (the registers OpenCSD's ocsd_etmv4_cfg consumes; see
    // Etmv4Registers in pipeline_config.hpp). TRCIDR3-7 and TRCAUTHSTATUS
    // are optional and default to 0 when absent. Extra top-level keys are
    // rejected.
    ParseResult ParseJson(std::string_view path) const;
};

}  // namespace config

#endif  // TRAILER_CONFIG_PARSER_CONFIG_PARSER_HPP_
