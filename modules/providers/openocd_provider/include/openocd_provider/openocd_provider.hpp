#ifndef TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_PROVIDER_HPP_
#define TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_PROVIDER_HPP_

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <trace_model/instruction_trace_decode_config.hpp>

#include "openocd_provider/memory_selector.hpp"
#include "openocd_provider/openocd_config.hpp"
#include "openocd_provider/trace_objects.hpp"

namespace providers {

class OpenOcdProvider {
 public:
    static std::expected<OpenOcdProvider, std::string> Create(const OpenOcdConfig& config);

    OpenOcdProvider(const OpenOcdProvider&) = delete;
    OpenOcdProvider& operator=(const OpenOcdProvider&) = delete;
    OpenOcdProvider(OpenOcdProvider&&) noexcept;
    OpenOcdProvider& operator=(OpenOcdProvider&&) noexcept;
    ~OpenOcdProvider();

    std::expected<std::string, std::string> GetCoreName();

    std::expected<std::vector<std::byte>, std::string> ReadMemory(const MemorySelector& selector, uint64_t address,
                                                                    uint32_t size);
    std::expected<void, std::string> WriteMemory(const MemorySelector& selector, uint64_t address,
                                                   const std::vector<std::byte>& data);

    std::expected<void, std::string> RunTclCommand(const std::string& command);

    std::expected<std::vector<TmcObject>, std::string> ListTraceSinks();
    std::expected<std::vector<Etmv4Object>, std::string> ListTraceSources();
    std::expected<model::Etmv4Registers, std::string> ReadETMv4Registers(const std::string& name);

    using TraceDataCallback = std::function<void(std::span<const std::byte> data, bool is_barrier)>;
    // TMC only for now. `callback` runs on OpenOCD's server thread, inside
    // the TARGET_EVENT_HALTED handler that drains the buffer - see arm_tmc.c.
    std::expected<void, std::string> SubscribeTrace(const std::string& name, TraceDataCallback callback);
    std::expected<void, std::string> UnsubscribeTrace(const std::string& name);

    std::expected<void, std::string> EnableTrace(const std::string& name);
    std::expected<void, std::string> DisableTrace(const std::string& name);

    // options is a literal Tcl 'configure' argument list
    std::expected<void, std::string> ConfigureTrace(const std::string& name, const std::string& options);

    enum class TargetStateEvent { kHalted, kResumed, kResetStart, kResetEnd, kExamineEnd };
    struct TargetStateChange {
        std::string target_name;
        TargetStateEvent event;
        bool halted;
    };

    using TargetStateCallback = std::function<void(const TargetStateChange&)>;
    // `callback` runs on OpenOCD's server thread, inside target_call_event_callbacks -
    // it must not call into LLDB, only record state or hand off to another thread.
    std::expected<void, std::string> SubscribeTargetState(TargetStateCallback callback);
    std::expected<void, std::string> UnsubscribeTargetState();

 private:
    OpenOcdProvider();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace providers

#endif  // TRAILER_PROVIDERS_OPENOCD_PROVIDER_OPENOCD_PROVIDER_HPP_
