#include "core/components/disassembly_manager.hpp"

#include <cstdlib>
#include <optional>
#include <string>

#include "core/debug_context.hpp"
#include "core/lldb_utils.hpp"
#include "dap/dap_error.hpp"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBInstruction.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBTarget.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"

namespace core {

namespace {

protocol::DisassembledInstruction GetInvalidInstruction() {
  protocol::DisassembledInstruction invalid_inst;
  invalid_inst.address = LLDB_INVALID_ADDRESS;
  invalid_inst.presentationHint =
      protocol::DisassembledInstruction::eDisassembledInstructionPresentationHintInvalid;
  return invalid_inst;
}

lldb::SBAddress GetDisassembleStartAddress(lldb::SBTarget target, lldb::SBAddress addr,
                                            int64_t instruction_offset) {
  if (instruction_offset == 0)
    return addr;

  if (target.GetMinimumOpcodeByteSize() == target.GetMaximumOpcodeByteSize()) {
    // We have fixed opcode size, so we can calculate the address directly,
    // negative or positive.
    lldb::addr_t load_addr = addr.GetLoadAddress(target);
    load_addr += instruction_offset * target.GetMinimumOpcodeByteSize();
    return lldb::SBAddress(load_addr, target);
  }

  if (instruction_offset > 0) {
    lldb::SBInstructionList forward_insts = target.ReadInstructions(addr, instruction_offset + 1);
    return forward_insts.GetInstructionAtIndex(forward_insts.GetSize() - 1).GetAddress();
  }

  // We have a negative instruction offset, so we need to disassemble backwards.
  // The opcode size is not fixed, so we have no idea where to start from.
  // Let's try from the start of the current symbol if available.
  auto symbol = addr.GetSymbol();
  if (!symbol.IsValid())
    return addr;

  // Add valid instructions before the current instruction using the symbol.
  lldb::SBInstructionList symbol_insts = target.ReadInstructions(symbol.GetStartAddress(), addr, nullptr);
  if (!symbol_insts.IsValid() || symbol_insts.GetSize() == 0)
    return addr;

  const auto backwards_instructions_count = static_cast<size_t>(std::abs(instruction_offset));
  if (symbol_insts.GetSize() < backwards_instructions_count) {
    // We don't have enough instructions to disassemble backwards, so just
    // return the start address of the symbol.
    return symbol_insts.GetInstructionAtIndex(0).GetAddress();
  }

  return symbol_insts.GetInstructionAtIndex(symbol_insts.GetSize() - backwards_instructions_count).GetAddress();
}

protocol::DisassembledInstruction ConvertSBInstructionToDisassembledInstruction(
    DebugContext &context, lldb::SBInstruction &inst, bool resolve_symbols) {
  lldb::SBTarget target = context.Target();
  if (!inst.IsValid())
    return GetInvalidInstruction();

  auto addr = inst.GetAddress();
  const auto inst_addr = addr.GetLoadAddress(target);

  // FIXME: This is a workaround - this address might come from
  // disassembly that started in a different section, and thus
  // comparisons between this object and other address objects with the
  // same load address will return false.
  addr = lldb::SBAddress(inst_addr, target);

  const char *m = inst.GetMnemonic(target);
  const char *o = inst.GetOperands(target);
  std::string c = inst.GetComment(target);
  auto d = inst.GetData(target);

  std::string bytes;
  llvm::raw_string_ostream sb(bytes);
  for (unsigned i = 0; i < inst.GetByteSize(); i++) {
    lldb::SBError error;
    uint8_t b = d.GetUnsignedInt8(error, i);
    if (error.Success())
      sb << llvm::format("%2.2x ", b);
  }

  protocol::DisassembledInstruction disassembled_inst;
  disassembled_inst.address = inst_addr;

  if (!bytes.empty()) // remove last whitespace
    bytes.pop_back();
  disassembled_inst.instructionBytes = std::move(bytes);

  llvm::raw_string_ostream si(disassembled_inst.instruction);
  si << llvm::formatv("{0,-7} {1,-25}", m, o);

  // Only add the symbol on the first line of the function.
  // in the comment section
  if (lldb::SBSymbol symbol = addr.GetSymbol(); symbol.GetStartAddress() == addr) {
    const llvm::StringRef sym_display_name = symbol.GetDisplayName();
    c.append(" ");
    c.append(sym_display_name);

    if (resolve_symbols)
      disassembled_inst.symbol = sym_display_name;
  }

  if (!c.empty())
    si << " ; " << c;

  std::optional<protocol::Source> source = context.ResolveSource(addr);
  lldb::SBLineEntry line_entry = GetLineEntryForAddress(target, addr);

  // If the line number is 0 then the entry represents a compiler generated
  // location.
  if (source && !IsAssemblySource(*source) && line_entry.GetStartAddress() == addr && line_entry.IsValid() &&
      line_entry.GetFileSpec().IsValid() && line_entry.GetLine() != 0) {

    disassembled_inst.location = std::move(source);
    const auto line = line_entry.GetLine();
    if (line != 0 && line != LLDB_INVALID_LINE_NUMBER)
      disassembled_inst.line = line;

    const auto column = line_entry.GetColumn();
    if (column != 0 && column != LLDB_INVALID_COLUMN_NUMBER)
      disassembled_inst.column = column;
  }

  return disassembled_inst;
}

}  // namespace

llvm::Expected<std::vector<protocol::DisassembledInstruction>>
DisassemblyManager::Disassemble(lldb::addr_t memory_reference, int64_t byte_offset,
                                 int64_t instruction_offset, uint64_t instruction_count,
                                 bool resolve_symbols) {
  if (memory_reference == LLDB_INVALID_ADDRESS) {
    return std::vector<protocol::DisassembledInstruction>(instruction_count, GetInvalidInstruction());
  }
  const lldb::addr_t addr_ptr = memory_reference + byte_offset;
  lldb::SBAddress addr(addr_ptr, m_context.Target());
  if (!addr.IsValid())
    return llvm::make_error<dap::DAPError>("Memory reference not found in the current binary.");

  // Calculate a sufficient address to start disassembling from.
  lldb::SBAddress disassemble_start_addr =
      GetDisassembleStartAddress(m_context.Target(), addr, instruction_offset);
  if (!disassemble_start_addr.IsValid())
    return llvm::make_error<dap::DAPError>("Unexpected error while disassembling instructions.");

  lldb::SBInstructionList insts = m_context.Target().ReadInstructions(disassemble_start_addr, instruction_count);
  if (!insts.IsValid())
    return llvm::make_error<dap::DAPError>("Unexpected error while disassembling instructions.");

  // Convert the found instructions to the DAP format.
  std::vector<protocol::DisassembledInstruction> instructions;
  size_t original_address_index = instruction_count;
  for (size_t i = 0; i < insts.GetSize(); ++i) {
    lldb::SBInstruction inst = insts.GetInstructionAtIndex(i);
    if (inst.GetAddress() == addr)
      original_address_index = i;

    instructions.push_back(ConvertSBInstructionToDisassembledInstruction(m_context, inst, resolve_symbols));
  }

  // Check if we miss instructions at the beginning.
  if (instruction_offset < 0) {
    const auto backwards_instructions_count = static_cast<size_t>(std::abs(instruction_offset));
    if (original_address_index < backwards_instructions_count) {
      // We don't have enough instructions before the main address as was
      // requested. Let's pad the start of the instructions with invalid
      // instructions.
      std::vector<protocol::DisassembledInstruction> invalid_instructions(
          backwards_instructions_count - original_address_index, GetInvalidInstruction());
      instructions.insert(instructions.begin(), invalid_instructions.begin(), invalid_instructions.end());

      // Trim excess instructions if needed.
      if (instructions.size() > instruction_count)
        instructions.resize(instruction_count);
    }
  }

  // Pad the instructions with invalid instructions if needed.
  while (instructions.size() < instruction_count)
    instructions.push_back(GetInvalidInstruction());

  return instructions;
}

}  // namespace core
