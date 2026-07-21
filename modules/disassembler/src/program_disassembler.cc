#include "disassembler/program_disassembler.hpp"

#include <iostream>
#include <mutex>
#include <optional>
#include <utility>

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

namespace disasm {

namespace {

// Registers the LLVM ARM target (TargetInfo/TargetMC/Disassembler) exactly
// once. Deliberately narrower than InitializeAllTargets(): only ARM is
// needed here, and calling the ARM-specific entry points avoids dragging in
// (and having to link) the other backends this build happens to include.
void EnsureArmTargetsRegistered() {
    static std::once_flag once;
    std::call_once(once, [] {
        LLVMInitializeARMTargetInfo();
        LLVMInitializeARMTargetMC();
        LLVMInitializeARMDisassembler();
    });
}

// Everything needed to disassemble one instruction set (Thumb or ARM/A32).
// Self-contained: each InstructionSet gets its own Target/context rather
// than sharing one, since (unlike llvm-objdump's use case) the caller
// always knows up front which ISA a range is in and never needs to
// interwork within a single decode.
struct IsaContext {
    const llvm::Target *target = nullptr;
    std::unique_ptr<llvm::MCRegisterInfo> reg_info;
    std::unique_ptr<llvm::MCAsmInfo> asm_info;
    std::unique_ptr<llvm::MCSubtargetInfo> subtarget_info;
    std::unique_ptr<llvm::MCInstrInfo> instr_info;
    std::unique_ptr<llvm::MCContext> context;
    std::unique_ptr<llvm::MCObjectFileInfo> object_file_info;
    std::unique_ptr<llvm::MCDisassembler> disassembler;
    std::unique_ptr<llvm::MCInstrAnalysis> instr_analysis;
    std::unique_ptr<llvm::MCInstPrinter> inst_printer;
    bool ready = false;
};

bool BuildIsaContext(IsaContext &ctx, llvm::StringRef triple_name, llvm::StringRef cpu, std::string &error) {
    const llvm::Triple triple(triple_name);

    std::string lookup_error;
    ctx.target = llvm::TargetRegistry::lookupTarget(triple, lookup_error);
    if (ctx.target == nullptr) {
        error = lookup_error;
        return false;
    }

    ctx.reg_info.reset(ctx.target->createMCRegInfo(triple));
    if (!ctx.reg_info) {
        error = "no register info for " + triple_name.str();
        return false;
    }

    const llvm::MCTargetOptions options;
    ctx.asm_info.reset(ctx.target->createMCAsmInfo(*ctx.reg_info, triple, options));
    if (!ctx.asm_info) {
        error = "no assembly info for " + triple_name.str();
        return false;
    }

    ctx.subtarget_info.reset(ctx.target->createMCSubtargetInfo(triple, cpu, /*Features=*/""));
    if (!ctx.subtarget_info) {
        error = "no subtarget info for " + triple_name.str();
        return false;
    }

    ctx.instr_info.reset(ctx.target->createMCInstrInfo());
    if (!ctx.instr_info) {
        error = "no instruction info for " + triple_name.str();
        return false;
    }

    ctx.context = std::make_unique<llvm::MCContext>(triple, ctx.asm_info.get(), ctx.reg_info.get(),
                                                      ctx.subtarget_info.get());
    ctx.object_file_info.reset(ctx.target->createMCObjectFileInfo(*ctx.context, /*PIC=*/false));
    ctx.context->setObjectFileInfo(ctx.object_file_info.get());

    ctx.disassembler.reset(ctx.target->createMCDisassembler(*ctx.subtarget_info, *ctx.context));
    if (!ctx.disassembler) {
        error = "no disassembler for " + triple_name.str();
        return false;
    }

    ctx.instr_analysis.reset(ctx.target->createMCInstrAnalysis(ctx.instr_info.get()));

    const int dialect = ctx.asm_info->getAssemblerDialect();
    ctx.inst_printer.reset(
        ctx.target->createMCInstPrinter(triple, dialect, *ctx.asm_info, *ctx.instr_info, *ctx.reg_info));
    if (!ctx.inst_printer) {
        error = "no instruction printer for " + triple_name.str();
        return false;
    }
    ctx.inst_printer->setPrintBranchImmAsAddress(true);
    if (ctx.instr_analysis) {
        ctx.inst_printer->setMCInstrAnalysis(ctx.instr_analysis.get());
    }

    ctx.ready = true;
    return true;
}

// Reads the PT_LOAD program headers of an ELFT-typed object file. Segments
// with no file-backed bytes (p_filesz == 0, e.g. the .bss tail of a
// segment) are skipped: there is nothing for a file-backed memory accessor
// to read for them.
template <typename ELFT>
void CollectLoadSegments(const llvm::object::ELFObjectFile<ELFT> &elf_obj, std::vector<model::LoadSegment> &out) {
    const auto &file = elf_obj.getELFFile();
    auto phdrs = file.program_headers();
    if (!phdrs) {
        llvm::consumeError(phdrs.takeError());
        return;
    }
    for (const auto &phdr : *phdrs) {
        if (phdr.p_type != llvm::ELF::PT_LOAD || phdr.p_filesz == 0) {
            continue;
        }
        out.push_back(model::LoadSegment{
            static_cast<uint64_t>(phdr.p_vaddr),
            static_cast<uint64_t>(phdr.p_offset),
            static_cast<uint64_t>(phdr.p_filesz),
        });
    }
}

bool ExtractLoadSegments(llvm::object::ObjectFile &obj, std::vector<model::LoadSegment> &out, std::string &error) {
    using namespace llvm::object;  // NOLINT: scoped to this function only.

    if (auto *elf = llvm::dyn_cast<ELF32LEObjectFile>(&obj)) {
        CollectLoadSegments(*elf, out);
        return true;
    }
    if (auto *elf = llvm::dyn_cast<ELF64LEObjectFile>(&obj)) {
        CollectLoadSegments(*elf, out);
        return true;
    }
    if (auto *elf = llvm::dyn_cast<ELF32BEObjectFile>(&obj)) {
        CollectLoadSegments(*elf, out);
        return true;
    }
    if (auto *elf = llvm::dyn_cast<ELF64BEObjectFile>(&obj)) {
        CollectLoadSegments(*elf, out);
        return true;
    }
    error = "unsupported ELF class/endianness";
    return false;
}

struct SectionWindow {
    uint64_t base_address;
    llvm::StringRef contents;
};

// Finds the section covering `address`. A TraceRecord instruction range is
// expected to lie entirely within one section; DisassembleRange logs a
// diagnostic and clips if that assumption doesn't hold.
std::optional<SectionWindow> FindSectionContaining(llvm::object::ObjectFile &obj, uint64_t address) {
    for (const llvm::object::SectionRef &section : obj.sections()) {
        const uint64_t addr = section.getAddress();
        const uint64_t size = section.getSize();
        if (size == 0 || address < addr || address >= addr + size) {
            continue;
        }
        llvm::Expected<llvm::StringRef> contents = section.getContents();
        if (!contents) {
            llvm::consumeError(contents.takeError());
            return std::nullopt;
        }
        return SectionWindow{addr, *contents};
    }
    return std::nullopt;
}

std::string FormatBytes(const uint8_t *data, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 3);
    for (size_t i = 0; i < len; ++i) {
        if (i != 0) {
            out.push_back(' ');
        }
        out.push_back(kHex[(data[i] >> 4) & 0xF]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

// LLVM's MCInstPrinter output is "\tmnemonic\toperands" (tab-separated,
// mirroring GNU assembler source line formatting). Split it back into the
// two fields callers actually want.
// LLVM's MCInstrAnalysis::isReturn() only recognizes the CodeGen-synthesized
// return pseudo (tBX_RET etc.); the disassembler never produces that pseudo,
// it always decodes the generic underlying instruction (e.g. plain tBX),
// which the ARM tablegen marks only as isIndirectBranch. So "is this a
// return" has to be recovered heuristically from the two conventional
// Thumb return idioms: "bx lr" and a multi-register pop that includes pc.
// String-based (on the already-printed mnemonic/operands) rather than
// inspecting MCInst register operands directly, to avoid reaching into
// ARM-internal register enums for what is fundamentally a display-level
// convention.
bool LooksLikeReturn(const std::string &mnemonic, const std::string &operands) {
    if (mnemonic == "bx") {
        return operands == "lr";
    }
    if (mnemonic.rfind("pop", 0) == 0) {
        return operands.find("pc") != std::string::npos;
    }
    return false;
}

void SplitMnemonicOperands(const std::string &text, std::string &mnemonic, std::string &operands) {
    const size_t start = text.find_first_not_of(" \t");
    if (start == std::string::npos) {
        mnemonic.clear();
        operands.clear();
        return;
    }
    const size_t sep = text.find_first_of(" \t", start);
    if (sep == std::string::npos) {
        mnemonic = text.substr(start);
        operands.clear();
        return;
    }
    mnemonic = text.substr(start, sep - start);
    const size_t operand_start = text.find_first_not_of(" \t", sep);
    operands = (operand_start == std::string::npos) ? std::string() : text.substr(operand_start);
}

}  // namespace

struct ProgramDisassembler::Impl {
    llvm::object::OwningBinary<llvm::object::Binary> owning_binary;
    llvm::object::ObjectFile *object_file = nullptr;  // Non-owning alias into owning_binary.
    std::vector<model::LoadSegment> load_segments;
    IsaContext thumb_ctx;
    IsaContext arm_ctx;

    bool Load(std::string_view elf_path, std::string &error) {
        EnsureArmTargetsRegistered();

        const std::string path(elf_path);
        llvm::Expected<llvm::object::OwningBinary<llvm::object::Binary>> binary_or_err =
            llvm::object::createBinary(path);
        if (!binary_or_err) {
            error = "disassembler: failed to load '" + path + "': " + llvm::toString(binary_or_err.takeError());
            return false;
        }
        owning_binary = std::move(*binary_or_err);

        object_file = llvm::dyn_cast<llvm::object::ObjectFile>(owning_binary.getBinary());
        if (object_file == nullptr) {
            error = "disassembler: '" + path + "' is not an object file";
            return false;
        }
        if (!object_file->isELF()) {
            error = "disassembler: '" + path + "' is not an ELF file";
            return false;
        }

        std::string segments_error;
        if (!ExtractLoadSegments(*object_file, load_segments, segments_error)) {
            error = "disassembler: " + segments_error;
            return false;
        }

        std::string thumb_error;
        if (!BuildIsaContext(thumb_ctx, "thumbv7em-none-eabi", "cortex-m7", thumb_error)) {
            error = "disassembler: failed to initialize Thumb decoder: " + thumb_error;
            return false;
        }

        // Kept for AArch32 completeness (interworking targets, A-profile
        // cores); Cortex-M has no A32 mode and never produces ocsd_isa_arm
        // ranges, so a failure here is non-fatal.
        std::string arm_error;
        if (!BuildIsaContext(arm_ctx, "armv7-none-eabi", "", arm_error)) {
            std::cerr << "disassembler: ARM (A32) decoder unavailable, Thumb-only: " << arm_error << "\n";
        }

        return true;
    }

    const IsaContext *GetContext(model::InstructionSet isa) const {
        switch (isa) {
            case model::InstructionSet::kThumb:
                return thumb_ctx.ready ? &thumb_ctx : nullptr;
            case model::InstructionSet::kArm:
                return arm_ctx.ready ? &arm_ctx : nullptr;
            case model::InstructionSet::kUnknown:
                return nullptr;
        }
        return nullptr;
    }
};

ProgramDisassembler::ProgramDisassembler() : impl_(std::make_unique<Impl>()) {}
ProgramDisassembler::ProgramDisassembler(ProgramDisassembler &&) noexcept = default;
ProgramDisassembler &ProgramDisassembler::operator=(ProgramDisassembler &&) noexcept = default;
ProgramDisassembler::~ProgramDisassembler() = default;

CreateResult ProgramDisassembler::Create(std::string_view elf_path) {
    CreateResult result;

    std::unique_ptr<ProgramDisassembler> disassembler(new ProgramDisassembler());
    std::string error;
    if (!disassembler->impl_->Load(elf_path, error)) {
        result.error = std::move(error);
        return result;
    }

    result.disassembler = std::move(disassembler);
    return result;
}

const std::vector<model::LoadSegment> &ProgramDisassembler::load_segments() const {
    return impl_->load_segments;
}

std::vector<model::DecodedInstruction> ProgramDisassembler::DisassembleRange(uint64_t start, uint64_t end,
                                                                              model::InstructionSet isa) const {
    std::vector<model::DecodedInstruction> instructions;

    const IsaContext *ctx = impl_->GetContext(isa);
    if (ctx == nullptr) {
        std::cerr << "disassembler: no decoder available for range [0x" << std::hex << start << ", 0x" << end
                   << std::dec << ")\n";
        return instructions;
    }
    if (start >= end) {
        return instructions;
    }

    const std::optional<SectionWindow> window = FindSectionContaining(*impl_->object_file, start);
    if (!window.has_value()) {
        std::cerr << "disassembler: no section covers address 0x" << std::hex << start << std::dec << "\n";
        return instructions;
    }

    uint64_t range_end = end;
    const uint64_t section_end = window->base_address + window->contents.size();
    if (range_end > section_end) {
        std::cerr << "disassembler: range [0x" << std::hex << start << ", 0x" << end << std::dec
                   << ") extends past its section end (0x" << std::hex << section_end << std::dec
                   << "), clipping\n";
        range_end = section_end;
    }

    const auto *section_bytes = reinterpret_cast<const uint8_t *>(window->contents.data());
    const uint64_t section_size = window->contents.size();

    uint64_t cur = start;
    while (cur < range_end) {
        const uint64_t offset = cur - window->base_address;
        const uint64_t available = section_size - offset;

        llvm::MCInst inst;
        uint64_t size = 0;
        const llvm::MCDisassembler::DecodeStatus status = ctx->disassembler->getInstruction(
            inst, size, llvm::ArrayRef<uint8_t>(section_bytes + offset, available), cur, llvm::nulls());

        const uint64_t min_step = (isa == model::InstructionSet::kThumb) ? 2 : 4;
        if (status == llvm::MCDisassembler::Fail) {
            std::cerr << "disassembler: failed to decode instruction at 0x" << std::hex << cur << std::dec << "\n";
            cur += (size != 0) ? size : min_step;
            continue;
        }
        if (size == 0) {
            // Shouldn't happen on a successful decode; guard against a
            // stuck loop rather than trust the backend unconditionally.
            std::cerr << "disassembler: zero-size instruction reported at 0x" << std::hex << cur << std::dec << "\n";
            cur += min_step;
            continue;
        }

        model::DecodedInstruction decoded;
        decoded.address = cur;
        decoded.size = static_cast<uint8_t>(size);
        decoded.isa = isa;
        decoded.bytes = FormatBytes(section_bytes + offset, size);

        std::string raw_text;
        {
            llvm::raw_string_ostream os(raw_text);
            ctx->inst_printer->printInst(&inst, cur, "", *ctx->subtarget_info, os);
        }
        SplitMnemonicOperands(raw_text, decoded.mnemonic, decoded.operands);
        decoded.text = decoded.operands.empty() ? decoded.mnemonic : decoded.mnemonic + " " + decoded.operands;

        if (ctx->instr_analysis) {
            decoded.is_indirect = ctx->instr_analysis->isIndirectBranch(inst);
            decoded.is_call = ctx->instr_analysis->isCall(inst);
            decoded.is_return =
                ctx->instr_analysis->isReturn(inst) || LooksLikeReturn(decoded.mnemonic, decoded.operands);
            decoded.is_conditional = ctx->instr_analysis->isConditionalBranch(inst);
            decoded.is_branch = decoded.is_call || decoded.is_return || decoded.is_indirect ||
                                 decoded.is_conditional || ctx->instr_analysis->isBranch(inst) ||
                                 ctx->instr_analysis->isUnconditionalBranch(inst);

            if (decoded.is_branch && !decoded.is_indirect) {
                uint64_t target = 0;
                if (ctx->instr_analysis->evaluateBranch(inst, cur, size, target)) {
                    decoded.branch_target = target;
                }
            }
        } else {
            decoded.is_branch = decoded.is_call = decoded.is_return = decoded.is_indirect = decoded.is_conditional =
                false;
        }

        instructions.push_back(std::move(decoded));
        cur += size;
    }

    return instructions;
}

}  // namespace disasm
