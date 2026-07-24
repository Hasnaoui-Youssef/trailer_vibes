#ifndef TRAILER_TRACE_DECODER_TRACE_DECODER_HPP_
#define TRAILER_TRACE_DECODER_TRACE_DECODER_HPP_

#include <expected>
#include <span>
#include <string>
#include <vector>

#include "trace_decoder/instruction_trace_decode_config.hpp"
#include "trace_model/load_segment.hpp"
#include "trace_sink/trace_record.hpp"

namespace decode {

// Decoding pipeline stage: turns an InstructionTraceDecodeConfig into the
// flat TraceRecord stream OpenCSD's ETMv4 decoder produces from it. Every
// OpenCSD detail (DecodeTree construction/lifetime, the ITrcGenElemIn sink,
// TraceDataIn chunking, datapath response codes) is private to this stage:
// a caller drives nothing beyond calling Decode() and never sees an
// OpenCSD type.
//
// Decode() is a value stage, not a resource holder: on success it returns
// the completed TraceRecord vector by value and is done - it does not
// presume how the caller will hold, share, or reuse those records
// afterward. Sharing across later pipeline stages is a decision for
// whatever assembles the pipeline (currently trailer's main(); a future
// Orchestrator, per TRACE_ANALYSIS_IMPLEMENTATION_PLAN.md), not for the
// decoder itself.
class TraceDecoder {
public:
    // Builds a decode tree for `config`, feeds it config.trace_data(), and
    // returns the resulting TraceRecords.
    //
    // `segments` are the firmware image's PT_LOAD regions (as extracted by
    // disasm::ProgramDisassembler::load_segments()); each is registered
    // with the decode tree's memory accessor so the ETMv4 decoder can read
    // opcode bytes from config.program_path() when resolving instruction
    // ranges. May be empty (e.g. in tests that only exercise decoder
    // construction), in which case decoding still runs but any real trace
    // fed to it will fail to resolve instruction ranges for lack of memory
    // access.
    //
    // On failure (tree/decoder construction failed, or decoding hit a
    // fatal datapath response), the error string explains what went wrong.
    // A non-fatal warning/error datapath response is logged to stderr but
    // still yields a successful result, matching this stage's previous
    // policy.
    std::expected<std::vector<trace::TraceRecord>, std::string> Decode(
        const InstructionTraceDecodeConfig &config, std::span<const model::LoadSegment> segments) const;
};

}  // namespace decode

#endif  // TRAILER_TRACE_DECODER_TRACE_DECODER_HPP_
