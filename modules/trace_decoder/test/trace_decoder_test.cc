// Deterministic smoke check for TraceDecoder: parses a known-good sample
// config and confirms a full decode run succeeds end to end (ETMv4 decoder
// construction + feeding the config's trace dump bytes through it) without
// a fatal datapath response. No load segments are supplied, so instruction
// ranges won't resolve opcodes - this only exercises decoder plumbing, not
// disassembly.
//
// Also exercises config-relative path resolution: TRAILER_SAMPLE_CONFIG_PATH
// is an absolute path, but the config's own "trace_dump"/"prog" entries are
// relative - ConfigParser::ParseJson must resolve them against the config
// file's directory, not this process's cwd, or TraceDecoder::Decode would
// fail to open the trace dump.

#include <cstdlib>
#include <expected>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "config_parser/config_parser.hpp"
#include "trace_decoder/trace_decoder.hpp"
#include "trace_model/load_segment.hpp"
#include "trace_sink/trace_record.hpp"

#ifndef TRAILER_SAMPLE_CONFIG_PATH
#error "TRAILER_SAMPLE_CONFIG_PATH must be defined by the build"
#endif

int main(int argc, char **argv) {
    const std::string config_path = argc > 1 ? argv[1] : TRAILER_SAMPLE_CONFIG_PATH;

    config::ConfigParser parser;
    const std::expected<config::PipelineConfig, std::string> parse_result = parser.ParseJson(config_path);
    if (!parse_result) {
        std::cerr << "trace_decoder_test: " << parse_result.error() << "\n";
        return EXIT_FAILURE;
    }

    decode::TraceDecoder decoder;
    const std::expected<std::vector<trace::TraceRecord>, std::string> decode_result =
        decoder.Decode(*parse_result, std::span<const model::LoadSegment>{});
    if (!decode_result) {
        std::cerr << "trace_decoder_test: " << decode_result.error() << "\n";
        return EXIT_FAILURE;
    }

    std::cerr << "trace_decoder_test: OK (" << decode_result->size() << " trace records)\n";
    return EXIT_SUCCESS;
}
