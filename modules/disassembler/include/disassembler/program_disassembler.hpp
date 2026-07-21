#ifndef TRAILER_DISASSEMBLER_PROGRAM_DISASSEMBLER_HPP_
#define TRAILER_DISASSEMBLER_PROGRAM_DISASSEMBLER_HPP_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "trace_model/decoded_instruction.hpp"
#include "trace_model/instruction_set.hpp"
#include "trace_model/load_segment.hpp"

namespace disasm {

class ProgramDisassembler;

// Result of loading a firmware image for disassembly. On success
// `disassembler` owns a ready ProgramDisassembler; on failure it is null and
// `error` explains what went wrong. Mirrors config::ParseResult /
// decode::BuildResult.
struct CreateResult {
    std::unique_ptr<ProgramDisassembler> disassembler;
    std::string error;

    bool Ok() const { return disassembler != nullptr; }
};

// Loads an ELF firmware image once via LLVM's Object/MC layer and
// disassembles address ranges from it on demand.
//
// This is the only component in the engine that includes LLVM headers or
// links LLVM libraries. It is compiled with -fno-rtti (LLVM's default) and
// must never be linked into a translation unit that also touches OpenCSD
// (which requires RTTI and uses dynamic_cast/inheritance internally). Its
// public interface is therefore LLVM-free: everything it exposes is a
// trace_model value type, so code on the OpenCSD side of the boundary
// (instr_reconstruct, trace_decoder, trailer) can include this header
// without ever seeing an LLVM type.
class ProgramDisassembler {
public:
    // Loads `elf_path`. Registers the ARM/Thumb LLVM targets on first call
    // (safe to call more than once, across instances).
    static CreateResult Create(std::string_view elf_path);

    ProgramDisassembler(ProgramDisassembler &&) noexcept;
    ProgramDisassembler &operator=(ProgramDisassembler &&) noexcept;
    ~ProgramDisassembler();

    // PT_LOAD segments (file offset + size, load address) read from the
    // ELF's program headers, for registering the image's opcode bytes with
    // an OpenCSD decode tree's memory accessor.
    const std::vector<model::LoadSegment> &load_segments() const;

    // Disassembles [start, end) as `isa`, walking instruction-sized steps
    // from `start`. Stops early (logging a diagnostic to stderr, never
    // throwing) if the range isn't fully covered by one ELF section, if
    // `isa` has no available decoder, or if an instruction fails to decode.
    std::vector<model::DecodedInstruction> DisassembleRange(uint64_t start, uint64_t end,
                                                             model::InstructionSet isa) const;

private:
    ProgramDisassembler();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace disasm

#endif  // TRAILER_DISASSEMBLER_PROGRAM_DISASSEMBLER_HPP_
