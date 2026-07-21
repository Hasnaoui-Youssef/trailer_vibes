#ifndef TRAILER_SOURCE_CORRELATOR_SOURCE_CORRELATOR_HPP_
#define TRAILER_SOURCE_CORRELATOR_SOURCE_CORRELATOR_HPP_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "trace_model/function_block.hpp"
#include "trace_model/reconstructed_instruction.hpp"
#include "trace_model/source_location.hpp"

namespace correlate {

class SourceCorrelator;

// Result of loading a firmware image's DWARF debug info. On success
// `correlator` owns a ready SourceCorrelator; on failure it is null and
// `error` explains what went wrong. Mirrors disasm::CreateResult /
// decode::BuildResult / config::ParseResult.
struct CreateResult {
    std::unique_ptr<SourceCorrelator> correlator;
    std::string error;

    bool Ok() const { return correlator != nullptr; }
};

// Resolves addresses to source locations via a firmware image's DWARF
// debug info, and groups a chronological instruction stream into line and
// function blocks.
//
// Like disasm::ProgramDisassembler, this is an LLVM-touching component
// (LLVM's DWARFContext): compiled with -fno-rtti, and must never be linked
// into a translation unit that also touches OpenCSD. It loads its own
// copy of the ELF independently of ProgramDisassembler - this pipeline
// stage's job is source correlation, not disassembly, and the two stages
// are kept decoupled rather than sharing a loaded-object cache between
// them (Architectural Rule 9: no component should own more than one
// responsibility). Its public interface exposes only trace_model value
// types, so it can be included and consumed from the OpenCSD/default-RTTI
// side of the engine.
class SourceCorrelator {
public:
    // Loads DWARF debug info from `elf_path`.
    static CreateResult Create(std::string_view elf_path);

    SourceCorrelator(SourceCorrelator &&) noexcept;
    SourceCorrelator &operator=(SourceCorrelator &&) noexcept;
    ~SourceCorrelator();

    // Resolves a single address to its full inline-frame chain (innermost
    // first). Returns a SourceLocation with an empty frame list if no
    // debug info covers `address`.
    model::SourceLocation Resolve(uint64_t address) const;

    // Walks `instructions` in trace order (instr_reconstruct's output),
    // considering only those with `executed == true`, and groups them
    // into function blocks, each holding its own chronological line
    // blocks. A new line block starts whenever the full inline-frame
    // chain changes; a new function block starts whenever the outermost
    // (real, non-inlined) enclosing function changes. Both loops and
    // recursion produce sibling blocks rather than being merged/summarized
    // - see LineBlock/FunctionBlock.
    std::vector<model::FunctionBlock> Correlate(
        const std::vector<model::ReconstructedInstruction> &instructions) const;

private:
    SourceCorrelator();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace correlate

#endif  // TRAILER_SOURCE_CORRELATOR_SOURCE_CORRELATOR_HPP_
