#ifndef TRAILER_DISASSEMBLER_PROGRAM_DISASSEMBLER_HPP_
#define TRAILER_DISASSEMBLER_PROGRAM_DISASSEMBLER_HPP_

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "trace_model/instruction_info.hpp"
#include "trace_model/load_segment.hpp"

namespace disasm {

// Loads an ELF firmware image once via LLVM's Object/MC/DWARF layers and
// precomputes, for the whole image, everything statically knowable about
// every instruction it contains: disassembly (mnemonic/operands/bytes/
// control-flow classification) and source correlation (DWARF file/line/
// inline-frame chain). This is the engine's single source of truth for
// instruction-level information - every later stage only ever looks values
// up here (by address) via InstructionInfoAt, never recomputes them.
//
// This is the only component in the engine that includes LLVM headers or
// links LLVM libraries (including DWARF parsing - it's the only ELF/DWARF
// owner in the engine, there's no separate source-correlation module
// duplicating that load). It is compiled with -fno-rtti (LLVM's default)
// and must never be linked into a translation unit that also touches
// OpenCSD (which requires RTTI and uses dynamic_cast/inheritance
// internally). Its public interface is therefore LLVM-free: everything it
// exposes is a trace_model value type, so code on the OpenCSD side of the
// boundary can include this header without ever seeing an LLVM type.
//
// Precomputation walks each executable section's ARM mapping symbols
// ($a/$t/$d) to distinguish real code from data-in-code (e.g. Thumb
// literal pools embedded in .text) rather than blindly linear-sweeping the
// whole section - the latter would decode literal-pool bytes as bogus
// instructions. A section with no mapping symbols at all is treated as one
// Thumb span (this engine's target is Cortex-M, Thumb-only).
class ProgramDisassembler {
public:
    // Loads `elf_path` and precomputes instruction info for the whole
    // image. Registers the ARM/Thumb LLVM targets on first call (safe to
    // call more than once, across instances). On failure, the error string
    // explains what went wrong. Missing/absent DWARF debug info is not a
    // failure - every InstructionInfo::location simply resolves empty.
    static std::expected<ProgramDisassembler, std::string> Create(const std::filesystem::path &elf_path);

    ProgramDisassembler(ProgramDisassembler &&) noexcept;
    ProgramDisassembler &operator=(ProgramDisassembler &&) noexcept;
    ~ProgramDisassembler();

    // PT_LOAD segments (file offset + size, load address) read from the
    // ELF's program headers, for registering the image's opcode bytes with
    // an OpenCSD decode tree's memory accessor.
    const std::vector<model::LoadSegment> &load_segments() const;

    // Looks up the precomputed instruction (disassembly + source location)
    // at `address`. Returns nullptr if `address` doesn't fall exactly on a
    // decoded instruction boundary the precompute pass covered (a data
    // span skipped via mapping symbols, a non-executable region, a
    // mid-instruction offset, or an address outside any executable
    // section).
    const model::InstructionInfo *InstructionInfoAt(uint64_t address) const;

private:
    ProgramDisassembler();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace disasm

#endif  // TRAILER_DISASSEMBLER_PROGRAM_DISASSEMBLER_HPP_
