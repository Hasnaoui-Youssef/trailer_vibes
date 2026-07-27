#include "openocd_provider/openocd_provider.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "command_queue.hpp"
#include "openocd_jmp.h"
#include "openocd_runtime.hpp"

namespace providers {

namespace {

std::atomic<bool> g_instance_alive{false};

void RunGuardedTrampoline(void* raw) { (*static_cast<std::function<void()>*>(raw))(); }

// Every call into OpenOCD goes through here, on whichever thread happens to
// be calling: the constructing thread during Create()'s synchronous
// build-up, the command-queue thread for everything after.
bool RunGuarded(std::function<void()> fn, int* exit_code) {
    return openocd_call_guarded(&RunGuardedTrampoline, &fn, exit_code) != 0;
}

int LogOutputHandler(struct command_context* context, const char* line) {
    auto* file = static_cast<FILE*>(context->output_handler_priv);
    std::fputs(line, file);
    return ERROR_OK;
}

TmcMode ToTmcMode(enum tmc_mode mode) {
    switch (mode) {
        case TMC_MODE_SW_FIFO:
            return TmcMode::kSwFifo;
        case TMC_MODE_HW_FIFO:
            return TmcMode::kHwFifo;
        case TMC_MODE_CIRC:
        default:
            return TmcMode::kCircular;
    }
}

TmcState ToTmcState(enum tmc_state state) {
    switch (state) {
        case TMC_STOPPED:
            return TmcState::kStopped;
        case TMC_DISABLING:
            return TmcState::kDisabling;
        case TMC_STOPPING:
            return TmcState::kStopping;
        case TMC_RUNNING:
            return TmcState::kRunning;
        case TMC_DISABLED:
        default:
            return TmcState::kDisabled;
    }
}

}  // namespace

struct OpenOcdProvider::Impl {
    struct command_context* cmd_ctx = nullptr;
    CommandQueue queue;
    FILE* log_file = nullptr;

    ~Impl() {
        if (!cmd_ctx) {
            if (log_file) std::fclose(log_file);
            return;
        }

        auto shutdown = [this]() { command_run_line(cmd_ctx, const_cast<char*>("shutdown")); };
        int exit_code = 0;
        if (queue.HasStarted()) {
            queue.RunSync([&]() { shutdown(); });
            queue.Join();
        } else {
            RunGuarded(shutdown, &exit_code);
        }

        RunGuarded(
            [this]() {
                server_quit();
                flash_free_all_banks();
                gdb_service_free();
                arm_tpiu_swo_cleanup_all();
                tmc_cleanup_all();
                etmv4_cleanup_all();
                server_free();
                unregister_all_commands(cmd_ctx, nullptr);
                help_del_all_commands(cmd_ctx);
                arm_cti_cleanup_all();
                dap_cleanup_all();
                adapter_quit();
                server_host_os_close();
                command_exit(cmd_ctx);
                rtt_exit();
                free_config();
                log_exit();
            },
            &exit_code);

        global_cmd_ctx = nullptr;
        if (log_file) std::fclose(log_file);
    }
};

OpenOcdProvider::OpenOcdProvider() : impl_(std::make_unique<Impl>()) {}
OpenOcdProvider::OpenOcdProvider(OpenOcdProvider&&) noexcept = default;
OpenOcdProvider& OpenOcdProvider::operator=(OpenOcdProvider&&) noexcept = default;
OpenOcdProvider::~OpenOcdProvider() {
    if (impl_) g_instance_alive.store(false);
}

std::expected<OpenOcdProvider, std::string> OpenOcdProvider::Create(const OpenOcdConfig& config) {
    bool expected = false;
    if (!g_instance_alive.compare_exchange_strong(expected, true)) {
        return std::unexpected("an OpenOcdProvider already exists in this process");
    }

    OpenOcdProvider provider;

    provider.impl_->log_file = std::fopen(config.log_file_path.c_str(), "w");
    if (!provider.impl_->log_file) {
        g_instance_alive.store(false);
        return std::unexpected("failed to open log file: " + config.log_file_path);
    }

    struct command_context* cmd_ctx = CreateCommandContext();
    if (!cmd_ctx) {
        g_instance_alive.store(false);
        return std::unexpected("failed to create the OpenOCD command context");
    }
    provider.impl_->cmd_ctx = cmd_ctx;

    int exit_code = 0;
    int retval = ERROR_OK;

    bool ok = RunGuarded(
        [&]() {
            util_init(cmd_ctx);
            rtt_init();
            command_context_mode(cmd_ctx, COMMAND_CONFIG);
            command_set_output_handler(cmd_ctx, &LogOutputHandler, provider.impl_->log_file);
            server_host_os_entry();

            for (const auto& dir : config.script_search_dirs) add_script_search_dir(dir.c_str());
            for (const auto& raw : config.raw_commands) add_config_command(raw.c_str());
            for (const auto& file : config.config_files) add_config_command(("script {" + file + "}").c_str());

            command_run_linef(cmd_ctx, const_cast<char*>("debug_level %d"), config.debug_level);
            command_run_linef(cmd_ctx, const_cast<char*>("gdb_port %s"), config.gdb_port.c_str());
            command_run_linef(cmd_ctx, const_cast<char*>("tcl_port %s"), config.tcl_port.c_str());
            command_run_linef(cmd_ctx, const_cast<char*>("telnet_port %s"), config.telnet_port.c_str());
        },
        &exit_code);
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during configuration");

    ok = RunGuarded([&]() { retval = parse_config_file(cmd_ctx); }, &exit_code);
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") while parsing config files");
    if (retval != ERROR_OK && retval != ERROR_COMMAND_CLOSE_CONNECTION) {
        return std::unexpected("parse_config_file() failed");
    }

    ok = RunGuarded([&]() { retval = server_init(cmd_ctx); }, &exit_code);
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during server_init()");
    if (retval != ERROR_OK) return std::unexpected("server_init() failed");

    if (config.init_at_startup) {
        ok = RunGuarded([&]() { retval = RunInit(cmd_ctx); }, &exit_code);
        if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during init");
        if (retval != ERROR_OK) return std::unexpected("the 'init' sequence failed");
    }

    auto queue_start = provider.impl_->queue.Start(cmd_ctx, config.wakeup_port);
    if (!queue_start) return std::unexpected(queue_start.error());

    return provider;
}

std::expected<std::vector<std::byte>, std::string> OpenOcdProvider::ReadMemory(const MemorySelector& selector,
                                                                                uint64_t address, uint32_t size) {
    struct command_context* cmd_ctx = impl_->cmd_ctx;
    std::vector<uint8_t> buffer(size);
    int retval = ERROR_FAIL;
    int exit_code = 0;

    bool ok = impl_->queue.RunSync([&]() {
        return RunGuarded(
            [&]() {
                struct target* target = selector.target_name.empty() ? get_current_target(cmd_ctx)
                                                                       : get_target(selector.target_name.c_str());
                if (!target) {
                    retval = ERROR_FAIL;
                    return;
                }
                retval = target_read_buffer(target, address, size, buffer.data());
            },
            &exit_code);
    });
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during ReadMemory");
    if (retval != ERROR_OK) return std::unexpected("target_read_buffer() failed");

    std::vector<std::byte> result(size);
    std::memcpy(result.data(), buffer.data(), size);
    return result;
}

std::expected<void, std::string> OpenOcdProvider::WriteMemory(const MemorySelector& selector, uint64_t address,
                                                                const std::vector<std::byte>& data) {
    struct command_context* cmd_ctx = impl_->cmd_ctx;
    int retval = ERROR_FAIL;
    int exit_code = 0;

    bool ok = impl_->queue.RunSync([&]() {
        return RunGuarded(
            [&]() {
                struct target* target = selector.target_name.empty() ? get_current_target(cmd_ctx)
                                                                       : get_target(selector.target_name.c_str());
                if (!target) {
                    retval = ERROR_FAIL;
                    return;
                }
                retval = target_write_buffer(target, address, static_cast<uint32_t>(data.size()),
                                              reinterpret_cast<const uint8_t*>(data.data()));
            },
            &exit_code);
    });
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during WriteMemory");
    if (retval != ERROR_OK) return std::unexpected("target_write_buffer() failed");
    return {};
}

std::expected<void, std::string> OpenOcdProvider::RunTclCommand(const std::string& command) {
    struct command_context* cmd_ctx = impl_->cmd_ctx;
    int exit_code = 0;
    int retval = ERROR_FAIL;

    bool ok = impl_->queue.RunSync([&]() {
        return RunGuarded([&]() { retval = command_run_line(cmd_ctx, const_cast<char*>(command.c_str())); },
                           &exit_code);
    });
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during RunTclCommand");
    if (retval != ERROR_OK) return std::unexpected("'" + command + "' failed");
    return {};
}

std::expected<std::vector<TmcObject>, std::string> OpenOcdProvider::ListTraceSinks() {
    std::vector<TmcObject> result;
    int exit_code = 0;
    bool ok = impl_->queue.RunSync([&]() {
        return RunGuarded(
            [&]() {
                tmc_for_each(
                    [](struct tmc_object* obj, void* arg) {
                        static_cast<std::vector<TmcObject>*>(arg)->push_back(TmcObject{
                            .name = obj->name,
                            .initialised = obj->initialised,
                            .mode = ToTmcMode(obj->mode),
                            .state = ToTmcState(obj->state),
                            .ap_num = obj->ap ? obj->spot.ap_num : 0,
                            .base = obj->spot.base,
                            .ram_size_words = obj->ram_size_words,
                        });
                    },
                    &result);
            },
            &exit_code);
    });
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during ListTraceSinks");
    return result;
}

std::expected<std::vector<Etmv4Object>, std::string> OpenOcdProvider::ListTraceSources() {
    std::vector<Etmv4Object> result;
    int exit_code = 0;
    bool ok = impl_->queue.RunSync([&]() {
        return RunGuarded(
            [&]() {
                etmv4_for_each(
                    [](struct etmv4_object* obj, void* arg) {
                        static_cast<std::vector<Etmv4Object>*>(arg)->push_back(Etmv4Object{
                            .name = etmv4_object_name(obj),
                            .initialised = etmv4_object_initialised(obj),
                            .enabled = etmv4_object_enabled(obj),
                            .trace_requested = etmv4_object_trace_requested(obj),
                            .ap_num = etmv4_object_ap_num(obj),
                            .base = etmv4_object_base(obj),
                            .traceid = etmv4_object_traceid(obj),
                        });
                    },
                    &result);
            },
            &exit_code);
    });
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during ListTraceSources");
    return result;
}

std::expected<void, std::string> OpenOcdProvider::ConfigureTrace(const std::string& name, const std::string& options) {
    struct command_context* cmd_ctx = impl_->cmd_ctx;
    int exit_code = 0;
    int retval = ERROR_FAIL;

    bool ok = impl_->queue.RunSync([&]() {
        return RunGuarded(
            [&]() {
                std::string line = name + " configure " + options;
                retval = command_run_line(cmd_ctx, const_cast<char*>(line.c_str()));
            },
            &exit_code);
    });
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during ConfigureTrace");
    if (retval != ERROR_OK) return std::unexpected("'" + name + " configure " + options + "' failed");
    return {};
}

std::expected<void, std::string> OpenOcdProvider::EnableTrace(const std::string& name) {
    int exit_code = 0;
    bool found = false;
    bool ok = impl_->queue.RunSync([&]() {
        return RunGuarded(
            [&]() {
                if (struct tmc_object* tmc = tmc_find_by_name(name.c_str())) {
                    found = true;
                    tmc_open_output(tmc);
                    tmc->capture_requested = true;
                    return;
                }
                if (struct etmv4_object* etmv4 = etmv4_find_by_name(name.c_str())) {
                    found = true;
                    etmv4_object_set_trace_requested(etmv4, true);
                }
            },
            &exit_code);
    });
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during EnableTrace");
    if (!found) return std::unexpected("no TMC or ETMv4 object named '" + name + "'");
    return {};
}

std::expected<void, std::string> OpenOcdProvider::DisableTrace(const std::string& name) {
    int exit_code = 0;
    bool found = false;
    bool ok = impl_->queue.RunSync([&]() {
        return RunGuarded(
            [&]() {
                if (struct tmc_object* tmc = tmc_find_by_name(name.c_str())) {
                    found = true;
                    tmc->capture_requested = false;
                    if (tmc->state == TMC_STOPPED) tmc_extract_data(tmc);
                    tmc_close_output(tmc);
                    return;
                }
                if (struct etmv4_object* etmv4 = etmv4_find_by_name(name.c_str())) {
                    found = true;
                    etmv4_object_set_trace_requested(etmv4, false);
                }
            },
            &exit_code);
    });
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during DisableTrace");
    if (!found) return std::unexpected("no TMC or ETMv4 object named '" + name + "'");
    return {};
}

std::expected<std::vector<std::byte>, std::string> OpenOcdProvider::ExtractTrace(const std::string& name) {
    int exit_code = 0;
    int retval = ERROR_FAIL;
    bool found = false;
    std::vector<std::byte> data;

    bool ok = impl_->queue.RunSync([&]() {
        return RunGuarded(
            [&]() {
                struct tmc_object* tmc = tmc_find_by_name(name.c_str());
                if (!tmc) return;
                found = true;
                retval = tmc_extract_data(tmc);
                if (retval != ERROR_OK || !tmc->file) return;
                std::rewind(tmc->file);
                std::byte buf[4096];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof(buf), tmc->file)) > 0) {
                    data.insert(data.end(), buf, buf + n);
                }
            },
            &exit_code);
    });
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during ExtractTrace");
    if (!found) return std::unexpected("no TMC object named '" + name + "'");
    if (retval != ERROR_OK) return std::unexpected("tmc_extract_data() failed");
    return data;
}

}  // namespace providers
