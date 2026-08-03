#include <algorithm>
#include <format>
#include <cstring>

#include "openocd_runtime.hpp"
#include "openocd_version.h"

extern "C" {
struct command_context* global_cmd_ctx = nullptr;
}

namespace providers {

namespace {

const char kStartupTcl[] = {
#include "startup_tcl.inc"
    0};

COMMAND_HANDLER(HandleVersionCommand) {
    if (CMD_ARGC > 1) return ERROR_COMMAND_SYNTAX_ERROR;
    const char* version = OPENOCD_PACKAGE_VERSION OPENOCD_RELSTR;
    if (CMD_ARGC == 1) {
        if (strcmp("git", CMD_ARGV[0])) return ERROR_COMMAND_ARGUMENT_INVALID;
        version = OPENOCD_GITVERSION;
    }
    command_print(CMD, "%s", version);
    return ERROR_OK;
}

COMMAND_HANDLER(HandleAddScriptSearchDirCommand) {
    if (CMD_ARGC != 1) return ERROR_COMMAND_SYNTAX_ERROR;
    add_script_search_dir(CMD_ARGV[0]);
    return ERROR_OK;
}

// TARGET_EVENT_GDB_START/_END suppress the halt banner while LLDB is the one
// driving halts over RSP; unrelated to any other provider state.
int LogTargetEventHandler(struct target* target, enum target_event event, void* /*priv*/) {
    switch (event) {
        case TARGET_EVENT_GDB_START:
            target->verbose_halt_msg = false;
            break;
        case TARGET_EVENT_GDB_END:
            target->verbose_halt_msg = true;
            break;
        case TARGET_EVENT_HALTED:
            if (target->verbose_halt_msg) target_arch_state(target);
            break;
        default:
            break;
    }
    return ERROR_OK;
}

int RunInitSequence(struct command_context* cmd_ctx) {
    bool save_poll_mask = jtag_poll_mask();

    int retval = command_run_line(cmd_ctx, const_cast<char*>("target init"));
    if (retval != ERROR_OK) return ERROR_FAIL;

    retval = adapter_init(cmd_ctx);
    if (retval != ERROR_OK) return retval;

    command_context_mode(cmd_ctx, COMMAND_EXEC);

    retval = command_run_line(cmd_ctx, const_cast<char*>("transport init"));
    if (retval != ERROR_OK) return ERROR_FAIL;

    retval = command_run_line(cmd_ctx, const_cast<char*>("dap init"));
    if (retval != ERROR_OK) return ERROR_FAIL;

    if (target_examine() != ERROR_OK) LOG_DEBUG("target examination failed");

    command_context_mode(cmd_ctx, COMMAND_CONFIG);

    if (command_run_line(cmd_ctx, const_cast<char*>("flash init")) != ERROR_OK) return ERROR_FAIL;
    if (command_run_line(cmd_ctx, const_cast<char*>("nand init")) != ERROR_OK) return ERROR_FAIL;
    if (command_run_line(cmd_ctx, const_cast<char*>("pld init")) != ERROR_OK) return ERROR_FAIL;

    command_context_mode(cmd_ctx, COMMAND_EXEC);

    if (command_run_line(cmd_ctx, const_cast<char*>("tpiu init")) != ERROR_OK) return ERROR_FAIL;
    if (command_run_line(cmd_ctx, const_cast<char*>("tmc init")) != ERROR_OK) return ERROR_FAIL;
    if (command_run_line(cmd_ctx, const_cast<char*>("etmv4 init")) != ERROR_OK) return ERROR_FAIL;

    jtag_poll_unmask(save_poll_mask);

    gdb_target_add_all(all_targets);
    target_register_event_callback(&LogTargetEventHandler, cmd_ctx);

    return command_run_line(cmd_ctx, const_cast<char*>("_run_post_init_commands"));
}

COMMAND_HANDLER(HandleInitCommand) {
    if (CMD_ARGC != 0) return ERROR_COMMAND_SYNTAX_ERROR;
    return RunInitSequence(CMD_CTX);
}

const struct command_registration kRuntimeCommandHandlers[] = {
    {
        "version",
        &HandleVersionCommand,
        nullptr,
        COMMAND_ANY,
        "show program version",
        "[git]",
        nullptr,
    },
    {
        "init",
        &HandleInitCommand,
        nullptr,
        COMMAND_ANY,
        "initialize configured targets and servers",
        "",
        nullptr,
    },
    {
        "add_script_search_dir",
        &HandleAddScriptSearchDirCommand,
        nullptr,
        COMMAND_ANY,
        "dir to search for config files and scripts",
        "<directory>",
        nullptr,
    },
    COMMAND_REGISTRATION_DONE,
};

using CommandRegistrant = int (*)(struct command_context*);

// Same order as openocd.c's original command_registrants[], minus the entry
// for openocd_register_commands itself - kRuntimeCommandHandlers above is
// its replacement.
const CommandRegistrant kSubsystemRegistrants[] = {
    &server_register_commands,       &gdb_register_commands,
    &log_register_commands,          &rtt_server_register_commands,
    &transport_register_commands,    &adapter_register_commands,
    &target_register_commands,       &flash_register_commands,
    &nand_register_commands,         &pld_register_commands,
    &cti_register_commands,          &dap_register_commands,
    &arm_tpiu_swo_register_commands, &arm_tmc_register_commands,
    &arm_etmv4_register_commands,
};

}  // namespace

struct command_context* CreateCommandContext() {
    log_init();
    struct command_context* cmd_ctx = command_init(kStartupTcl, nullptr);
    if (!cmd_ctx) return nullptr;

    if (register_commands(cmd_ctx, nullptr, kRuntimeCommandHandlers) != ERROR_OK) {
        command_done(cmd_ctx);
        return nullptr;
    }
    for (CommandRegistrant registrant : kSubsystemRegistrants) {
        if (registrant(cmd_ctx) != ERROR_OK) {
            command_done(cmd_ctx);
            return nullptr;
        }
    }

    global_cmd_ctx = cmd_ctx;
    return cmd_ctx;
}

namespace {

std::string ToTclSafeArg(std::string path) {
    std::ranges::replace(path, '\\', '/');
    return "{" + path + "}";
}

}  // namespace

std::vector<std::string> RegisterConfigCommands(const OpenOcdConfig& config) {
    std::vector<std::string> commands{};
    commands.reserve(config.config_files.size() + config.raw_commands.size());
    for (const auto& cfg : config.config_files) {
        commands.push_back(std::format("script {}", ToTclSafeArg(cfg)));
    }
    for (const auto& cmd : config.raw_commands) {
        commands.push_back(cmd);
    }
    return commands;
}

int RunInit(struct command_context* cmd_ctx) { return RunInitSequence(cmd_ctx); }

}  // namespace providers
