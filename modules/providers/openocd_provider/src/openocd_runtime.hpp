#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_RUNTIME_HPP_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_RUNTIME_HPP_

#include "openocd_c_api.hpp"
#include "openocd_provider/openocd_config.hpp"
#include <string>

extern "C" {
extern struct command_context* global_cmd_ctx;
}

namespace providers {

// Replaces openocd.c's setup_command_handler(): creates the Jim interpreter
// and command_context, defines global_cmd_ctx (helper/command.c's
// current_command_context() fallback needs it), and registers our
// init/version/add_script_search_dir plus every OpenOCD subsystem's
// commands. Returns nullptr on failure.
struct command_context* CreateCommandContext();
std::vector<std::string> RegisterConfigCommands(const OpenOcdConfig& config);
std::string ToTclSafeArg(std::string path);

// The "init" Tcl command's body, exposed so the provider can also call it
// directly without going through Tcl.
int RunInit(struct command_context* cmd_ctx);

}  // namespace providers

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_RUNTIME_HPP_
