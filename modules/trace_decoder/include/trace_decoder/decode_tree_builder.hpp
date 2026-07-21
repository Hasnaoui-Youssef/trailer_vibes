#ifndef TRAILER_TRACE_DECODER_DECODE_TREE_BUILDER_HPP_
#define TRAILER_TRACE_DECODER_DECODE_TREE_BUILDER_HPP_

#include <memory>
#include <span>
#include <string>

#include <opencsd.h>

#include "config_parser/pipeline_config.hpp"
#include "trace_model/load_segment.hpp"
#include "trace_sink/trace_record_sink.hpp"

namespace decode {

// Frees a DecodeTree created via DecodeTree::CreateDecodeTree.
struct DecodeTreeDeleter {
    void operator()(DecodeTree *tree) const {
        if (tree != nullptr) {
            DecodeTree::DestroyDecodeTree(tree);
        }
    }
};

using DecodeTreePtr = std::unique_ptr<DecodeTree, DecodeTreeDeleter>;

// Result of building a decode tree. On success `tree` owns a DecodeTree with
// a full ETMv4 decoder already attached and wired to the caller's sink; on
// failure `tree` is null and `error` explains what went wrong.
struct BuildResult {
    DecodeTreePtr tree;
    std::string error;

    bool Ok() const { return tree != nullptr; }
};

// Decoding-stage pipeline builder: turns a config::PipelineConfig into an
// OpenCSD DecodeTree configured with a full ETMv4 decoder, ready to accept
// raw trace bytes via DecodeTree::TraceDataIn(). This mirrors the
// snapshot_parser module's CreateDcdTreeFromSnapShot, but is driven by the
// config-loading stage's PipelineConfig rather than an Arm debug/trace
// snapshot.
class DecodeTreeBuilder {
public:
    // Builds a decode tree for `config`, attaching `sink` as the generic
    // trace element output. `sink` must outlive the returned tree.
    //
    // `segments` are the firmware image's PT_LOAD regions (as extracted by
    // disasm::ProgramDisassembler::load_segments()); each is registered with
    // the decode tree's memory accessor so the ETMv4 decoder can read
    // opcode bytes from config.program_path when resolving instruction
    // ranges. May be empty (e.g. in tests that only exercise decoder
    // construction), in which case the tree is built but any real trace fed
    // to it will fail to resolve instruction ranges for lack of memory
    // access.
    BuildResult Build(const config::PipelineConfig &config, std::span<const model::LoadSegment> segments,
                       trace::TraceRecordSink &sink) const;
};

}  // namespace decode

#endif  // TRAILER_TRACE_DECODER_DECODE_TREE_BUILDER_HPP_
