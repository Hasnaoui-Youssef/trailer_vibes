// trailer-dap will link both liblldb (which embeds its own LLVM) and
// disassembler (~20 LLVM component libraries). A link-only test proves
// nothing - static archives only pull in objects that resolve undefined
// symbols. This test makes real calls into both, in the same order
// dap_main.cc uses (LLDB first), to catch startup failures like
// "CommandLine Error: Option '...' registered more than once!" before
// they show up in the real adapter.

#include <cstdlib>
#include <expected>
#include <iostream>
#include <string>

#include "disassembler/program_disassembler.hpp"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBTarget.h"

#ifndef TRAILER_FIRMWARE_PATH
#error "TRAILER_FIRMWARE_PATH must be defined by the build"
#endif

namespace {

int Fail(const std::string &message) {
    std::cerr << "llvm_lldb_coexistence_test: " << message << "\n";
    return EXIT_FAILURE;
}

}  // namespace

int main() {
    const lldb::SBError lldb_init_error = lldb::SBDebugger::InitializeWithErrorHandling();
    if (lldb_init_error.Fail()) {
        return Fail(std::string("LLDB init failed: ") + lldb_init_error.GetCString());
    }

    std::expected<disasm::ProgramDisassembler, std::string> disassembler_result =
        disasm::ProgramDisassembler::Create(TRAILER_FIRMWARE_PATH);
    if (!disassembler_result) {
        lldb::SBDebugger::Terminate();
        return Fail("ProgramDisassembler::Create failed: " + disassembler_result.error());
    }
    const disasm::ProgramDisassembler &disassembler = *disassembler_result;

    if (disassembler.load_segments().empty()) {
        lldb::SBDebugger::Terminate();
        return Fail("ProgramDisassembler loaded zero PT_LOAD segments");
    }

    lldb::SBDebugger debugger = lldb::SBDebugger::Create();
    if (!debugger.IsValid()) {
        lldb::SBDebugger::Terminate();
        return Fail("SBDebugger::Create returned an invalid debugger");
    }

    lldb::SBError target_error;
    lldb::SBTarget target =
        debugger.CreateTarget(TRAILER_FIRMWARE_PATH, /*target_triple=*/nullptr, /*platform_name=*/nullptr,
                               /*add_dependent_modules=*/false, target_error);
    if (target_error.Fail() || !target.IsValid()) {
        lldb::SBDebugger::Destroy(debugger);
        lldb::SBDebugger::Terminate();
        return Fail(std::string("SBDebugger::CreateTarget failed: ") + target_error.GetCString());
    }

    std::cout << "llvm_lldb_coexistence_test: load_segments=" << disassembler.load_segments().size() << "\n";

    lldb::SBDebugger::Destroy(debugger);
    lldb::SBDebugger::Terminate();
    return EXIT_SUCCESS;
}
