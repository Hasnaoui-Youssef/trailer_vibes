#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_CONFIG_HPP_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_CONFIG_HPP_

#include <string>
#include <vector>

namespace providers {

struct OpenOcdConfig {
    std::vector<std::string> script_search_dirs;
    std::vector<std::string> config_files;
    std::vector<std::string> raw_commands;
    std::string log_file_path = "openocd_logs.txt";
    int debug_level = 2;
    bool init_at_startup = true;
    std::string gdb_port = "3333";
    std::string tcl_port = "disabled";
    std::string telnet_port = "disabled";
    std::string wakeup_port = "6600";
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_CONFIG_HPP_
