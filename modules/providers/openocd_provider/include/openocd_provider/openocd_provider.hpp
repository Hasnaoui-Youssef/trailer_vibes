#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_PROVIDER_HPP_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_PROVIDER_HPP_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "openocd_provider/memory_selector.hpp"
#include "openocd_provider/openocd_config.hpp"
#include "openocd_provider/trace_objects.hpp"

namespace providers {

// One instance per process (see Phase 0: all_targets, adapter_driver and
// the rest of OpenOCD's state are process-wide globals, not owned by this
// object). Create() enforces that with a static guard.
class OpenOcdProvider {
 public:
    static std::expected<OpenOcdProvider, std::string> Create(const OpenOcdConfig& config);

    OpenOcdProvider(const OpenOcdProvider&) = delete;
    OpenOcdProvider& operator=(const OpenOcdProvider&) = delete;
    OpenOcdProvider(OpenOcdProvider&&) noexcept;
    OpenOcdProvider& operator=(OpenOcdProvider&&) noexcept;
    ~OpenOcdProvider();

    std::expected<std::vector<std::byte>, std::string> ReadMemory(const MemorySelector& selector, uint64_t address,
                                                                    uint32_t size);
    std::expected<void, std::string> WriteMemory(const MemorySelector& selector, uint64_t address,
                                                   const std::vector<std::byte>& data);

    // Escape hatch for anything not covered by the API above (reset
    // strategies, adapter diagnostics, ...): breakpoints/execution/variables
    // stay LLDB's job, so this module never grows a first-class API for
    // them.
    std::expected<void, std::string> RunTclCommand(const std::string& command);

    std::expected<std::vector<TmcObject>, std::string> ListTraceSinks();
    std::expected<std::vector<Etmv4Object>, std::string> ListTraceSources();
    std::expected<void, std::string> EnableTrace(const std::string& name);
    std::expected<void, std::string> DisableTrace(const std::string& name);
    std::expected<std::vector<std::byte>, std::string> ExtractTrace(const std::string& name);

    // options is a literal Tcl 'configure' argument list, e.g.
    // "-mode circular -bufwm 4096" - going through the same in-process Tcl
    // command a config file would use, not a remote service (see arm_tmc.h's
    // jim_tmc_configure / arm_etmv4.c's equivalent for what this accepts).
    std::expected<void, std::string> ConfigureTrace(const std::string& name, const std::string& options);

 private:
    OpenOcdProvider();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_PROVIDER_HPP_
