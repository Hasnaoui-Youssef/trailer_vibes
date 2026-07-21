// Deterministic smoke check for DecodeTreeBuilder: parses a known-good
// sample config and confirms a full ETMv4 decode tree can actually be
// constructed from it (DecodeTree::createDecoder succeeds). No trace bytes
// or memory image are needed for this - those are only required once
// decoding of real trace data begins, so this test passes an empty load
// segment span rather than actually disassembling a firmware image.

#include <cstdlib>
#include <iostream>
#include <span>
#include <string>

#include "config_parser/config_parser.hpp"
#include "trace_decoder/decode_tree_builder.hpp"
#include "trace_model/load_segment.hpp"
#include "trace_sink/trace_record_sink.hpp"

#ifndef TRAILER_SAMPLE_CONFIG_PATH
#error "TRAILER_SAMPLE_CONFIG_PATH must be defined by the build"
#endif

int main(int argc, char **argv) {
    const std::string config_path = argc > 1 ? argv[1] : TRAILER_SAMPLE_CONFIG_PATH;

    config::ConfigParser parser;
    const config::ParseResult parse_result = parser.ParseJson(config_path);
    if (!parse_result.Ok()) {
        std::cerr << "decode_tree_builder_smoke: " << parse_result.error << "\n";
        return EXIT_FAILURE;
    }

    trace::TraceRecordSink sink;
    decode::DecodeTreeBuilder builder;
    const decode::BuildResult build_result =
        builder.Build(*parse_result.config, std::span<const model::LoadSegment>{}, sink);
    if (!build_result.Ok()) {
        std::cerr << "decode_tree_builder_smoke: " << build_result.error << "\n";
        return EXIT_FAILURE;
    }

    std::cerr << "decode_tree_builder_smoke: OK\n";
    return EXIT_SUCCESS;
}
