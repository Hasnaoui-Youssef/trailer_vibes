#include "source_correlator/source_correlator.hpp"

#include <utility>

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"

namespace correlate {

namespace {

llvm::DILineInfoSpecifier MakeSpecifier() {
    return llvm::DILineInfoSpecifier(llvm::DILineInfoSpecifier::FileLineInfoKind::RawValue,
                                      llvm::DILineInfoSpecifier::FunctionNameKind::ShortName);
}

// Whether two LineBlocks (identified here just by their locations) should
// merge into the same FunctionBlock: true when they share the same
// outermost (real, non-inlined) enclosing function. Two unresolved
// locations (no debug info) are treated as the same "unknown" function,
// so a run of undebuggable instructions collapses into one block instead
// of one per instruction.
bool SameFunction(const model::SourceLocation &a, const model::SourceLocation &b) {
    const bool a_empty = a.frames.empty();
    const bool b_empty = b.frames.empty();
    if (a_empty != b_empty) {
        return false;
    }
    if (a_empty) {
        return true;
    }
    return a.frames.back().function == b.frames.back().function;
}

}  // namespace

struct SourceCorrelator::Impl {
    llvm::object::OwningBinary<llvm::object::Binary> owning_binary;
    llvm::object::ObjectFile *object_file = nullptr;  // Non-owning alias into owning_binary.
    std::unique_ptr<llvm::DWARFContext> dwarf_context;

    bool Load(std::string_view elf_path, std::string &error) {
        const std::string path(elf_path);
        llvm::Expected<llvm::object::OwningBinary<llvm::object::Binary>> binary_or_err =
            llvm::object::createBinary(path);
        if (!binary_or_err) {
            error = "source_correlator: failed to load '" + path + "': " + llvm::toString(binary_or_err.takeError());
            return false;
        }
        owning_binary = std::move(*binary_or_err);

        object_file = llvm::dyn_cast<llvm::object::ObjectFile>(owning_binary.getBinary());
        if (object_file == nullptr) {
            error = "source_correlator: '" + path + "' is not an object file";
            return false;
        }

        dwarf_context = llvm::DWARFContext::create(*object_file);
        if (!dwarf_context) {
            error = "source_correlator: failed to parse DWARF debug info in '" + path + "'";
            return false;
        }
        return true;
    }

    model::SourceLocation Resolve(uint64_t address) const {
        const llvm::DIInliningInfo inlining = dwarf_context->getInliningInfoForAddress(
            {address, llvm::object::SectionedAddress::UndefSection}, MakeSpecifier());

        model::SourceLocation location;
        for (uint32_t i = 0; i < inlining.getNumberOfFrames(); ++i) {
            const llvm::DILineInfo &frame = inlining.getFrame(i);

            model::InlineFrame inline_frame;
            inline_frame.function =
                frame.FunctionName == llvm::DILineInfo::BadString ? std::string() : frame.FunctionName;
            inline_frame.file = frame.FileName == llvm::DILineInfo::BadString ? std::string() : frame.FileName;
            inline_frame.line = frame.Line;
            inline_frame.column = frame.Column;
            location.frames.push_back(std::move(inline_frame));
        }
        return location;
    }
};

SourceCorrelator::SourceCorrelator() : impl_(std::make_unique<Impl>()) {}
SourceCorrelator::SourceCorrelator(SourceCorrelator &&) noexcept = default;
SourceCorrelator &SourceCorrelator::operator=(SourceCorrelator &&) noexcept = default;
SourceCorrelator::~SourceCorrelator() = default;

CreateResult SourceCorrelator::Create(std::string_view elf_path) {
    CreateResult result;

    std::unique_ptr<SourceCorrelator> correlator(new SourceCorrelator());
    std::string error;
    if (!correlator->impl_->Load(elf_path, error)) {
        result.error = std::move(error);
        return result;
    }

    result.correlator = std::move(correlator);
    return result;
}

model::SourceLocation SourceCorrelator::Resolve(uint64_t address) const { return impl_->Resolve(address); }

std::vector<model::FunctionBlock> SourceCorrelator::Correlate(
    const std::vector<model::ReconstructedInstruction> &instructions) const {
    std::vector<model::FunctionBlock> functions;

    for (const model::ReconstructedInstruction &instruction : instructions) {
        if (!instruction.executed) {
            continue;
        }

        const uint64_t address = instruction.insn.address;
        model::SourceLocation location = impl_->Resolve(address);

        const bool need_new_line_block = functions.empty() || functions.back().line_blocks.empty() ||
                                          functions.back().line_blocks.back().location != location;

        if (need_new_line_block) {
            const bool need_new_function_block =
                functions.empty() || !SameFunction(functions.back().line_blocks.back().location, location);

            if (need_new_function_block) {
                model::FunctionBlock function_block;
                function_block.function_name = location.frames.empty() ? std::string() : location.frames.back().function;
                function_block.entry_addr = address;
                functions.push_back(std::move(function_block));
            }

            model::LineBlock line_block;
            line_block.location = std::move(location);
            line_block.start_addr = address;
            line_block.end_addr = address + instruction.insn.size;
            line_block.instr_count = 1;
            line_block.trace_index = instruction.trace_index;
            line_block.trace_id = instruction.trace_id;
            functions.back().line_blocks.push_back(std::move(line_block));
        } else {
            model::LineBlock &line_block = functions.back().line_blocks.back();
            line_block.end_addr = address + instruction.insn.size;
            line_block.instr_count += 1;
        }
    }

    return functions;
}

}  // namespace correlate
