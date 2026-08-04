#include "openocd_provider/openocd_provider.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unordered_map>

#include "command_queue.hpp"
#include "openocd_jmp.h"
#include "openocd_runtime.hpp"

namespace providers {

namespace {

std::string ResolveLogFilePath(const std::string& configured_path) {
    std::filesystem::path path(configured_path);
    if (path.is_absolute()) {
        return configured_path;
    }

#ifdef _WIN32
    const char* app_data = std::getenv("LOCALAPPDATA");
    std::filesystem::path base_dir = app_data ? std::filesystem::path(app_data) : std::filesystem::temp_directory_path();
#else
    const char* xdg_state = std::getenv("XDG_STATE_HOME");
    std::filesystem::path base_dir;
    if (xdg_state && *xdg_state) {
        base_dir = xdg_state;
    } else if (const char* home = std::getenv("HOME")) {
        base_dir = std::filesystem::path(home) / ".local" / "state";
    } else {
        base_dir = std::filesystem::temp_directory_path();
    }
#endif
    base_dir /= "Trailer";
    std::error_code ec;
    std::filesystem::create_directories(base_dir, ec);
    // If this failed, the fopen() below will surface a clear "failed to open
    // log file" error - no need to duplicate that handling here.
    return (base_dir / path).string();
}

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

void TraceCallbackTrampoline(const uint8_t* data, size_t size, bool is_barrier, void* args) {
    auto* callback = static_cast<OpenOcdProvider::TraceDataCallback*>(args);
    (*callback)(std::span(reinterpret_cast<const std::byte*>(data), size), is_barrier);
}

std::optional<OpenOcdProvider::TargetStateEvent> MapTargetEvent(enum target_event event) {
    switch (event) {
        case TARGET_EVENT_HALTED:
            return OpenOcdProvider::TargetStateEvent::kHalted;
        case TARGET_EVENT_RESUMED:
            return OpenOcdProvider::TargetStateEvent::kResumed;
        case TARGET_EVENT_RESET_START:
            return OpenOcdProvider::TargetStateEvent::kResetStart;
        case TARGET_EVENT_RESET_END:
            return OpenOcdProvider::TargetStateEvent::kResetEnd;
        case TARGET_EVENT_EXAMINE_END:
            return OpenOcdProvider::TargetStateEvent::kExamineEnd;
        default:
            return std::nullopt;
    }
}

int TargetStateTrampoline(struct target* target, enum target_event event, void* args) {
    std::optional<OpenOcdProvider::TargetStateEvent> mapped = MapTargetEvent(event);
    if (!mapped) return ERROR_OK;
    auto* callback = static_cast<OpenOcdProvider::TargetStateCallback*>(args);
    (*callback)(OpenOcdProvider::TargetStateChange{target_name(target), *mapped, target->state == TARGET_HALTED});
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
    std::unordered_map<std::string, OpenOcdProvider::TraceDataCallback> trace_callbacks;
    OpenOcdProvider::TargetStateCallback target_state_callback;

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
                for (const auto& [name, callback] : trace_callbacks) {
                    if (struct tmc_object* tmc = tmc_find_by_name(name.c_str())) tmc_clear_capture_callback(tmc);
                }
                trace_callbacks.clear();
                if (target_state_callback) {
                    target_unregister_event_callback(&TargetStateTrampoline, &target_state_callback);
                    target_state_callback = nullptr;
                }

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

    const std::string log_file_path = ResolveLogFilePath(config.log_file_path);
    provider.impl_->log_file = std::fopen(log_file_path.c_str(), "w");
    if (!provider.impl_->log_file) {
        g_instance_alive.store(false);
        return std::unexpected("failed to open log file: " + log_file_path);
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

            //Can't really avoid this as it's registered for ocd_find fed to "find" TCL commands
            for (const auto& dir : config.script_search_dirs) add_script_search_dir(dir.c_str());

            command_run_linef(cmd_ctx, const_cast<char*>("debug_level %d"), config.debug_level);
            command_run_linef(cmd_ctx, const_cast<char*>("gdb_port %s"), config.gdb_port.c_str());
            command_run_linef(cmd_ctx, const_cast<char*>("tcl_port %s"), config.tcl_port.c_str());
            command_run_linef(cmd_ctx, const_cast<char*>("telnet_port %s"), config.telnet_port.c_str());
        },
        &exit_code);
    if (!ok) return std::unexpected("openocd_exit(" + std::to_string(exit_code) + ") during configuration");
    auto commands = RegisterConfigCommands(config);
    ok = RunGuarded([&]() {
        if(commands.empty()) {
            command_run_line(cmd_ctx, const_cast<char*>("script openocd.cfg"));
            retval = ERROR_OK;
            return;
        }
        for(auto& cmd : commands) {
            retval = command_run_line(cmd_ctx, cmd.data());
            if (retval != ERROR_OK) return;
        }
        retval = ERROR_OK;
    }, &exit_code);
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


std::expected<std::string, std::string> OpenOcdProvider::GetCoreName() {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        std::string name;
        std::string failure;
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([]() {
        Outcome out;
        out.ok = RunGuarded(
            [&]() {
                for (struct target* target = all_targets; target; target = target->next) {
                    if (std::strcmp(target_type_name(target), "mem_ap") == 0) continue;
                    struct cortex_m_common* cm = target_to_cortex_m_safe(target);
                    if (!cm) {
                        out.failure = "target '" + std::string(target_name(target)) + "' is not a Cortex-M core";
                        return;
                    }
                    if (!cm->core_info) {
                        out.failure = "target '" + std::string(target_name(target)) + "' has not been examined yet";
                        return;
                    }
                    out.name = cm->core_info->name;
                    return;
                }
                out.failure = "no non-AP target found";
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during GetCoreName");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during GetCoreName");
    if (!outcome->failure.empty()) return std::unexpected(outcome->failure);
    return outcome->name;
}

std::expected<std::vector<std::byte>, std::string> OpenOcdProvider::ReadMemory(const MemorySelector& selector,
                                                                                uint64_t address, uint32_t size) {
    struct command_context* cmd_ctx = impl_->cmd_ctx;

    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        int retval = ERROR_FAIL;
        std::vector<uint8_t> buffer;
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([cmd_ctx, selector, address, size]() {
        Outcome out;
        out.buffer.resize(size);
        out.ok = RunGuarded(
            [&]() {
                struct target* target = selector.target_name.empty() ? get_current_target(cmd_ctx)
                                                                       : get_target(selector.target_name.c_str());
                if (!target) {
                    out.retval = ERROR_FAIL;
                    return;
                }
                out.retval = target_read_buffer(target, address, size, out.buffer.data());
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during ReadMemory");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during ReadMemory");
    if (outcome->retval != ERROR_OK) return std::unexpected("target_read_buffer() failed");

    std::vector<std::byte> result(size);
    std::memcpy(result.data(), outcome->buffer.data(), size);
    return result;
}

std::expected<void, std::string> OpenOcdProvider::WriteMemory(const MemorySelector& selector, uint64_t address,
                                                                const std::vector<std::byte>& data) {
    struct command_context* cmd_ctx = impl_->cmd_ctx;

    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        int retval = ERROR_FAIL;
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([cmd_ctx, selector, address, data]() {
        Outcome out;
        out.ok = RunGuarded(
            [&]() {
                struct target* target = selector.target_name.empty() ? get_current_target(cmd_ctx)
                                                                       : get_target(selector.target_name.c_str());
                if (!target) {
                    out.retval = ERROR_FAIL;
                    return;
                }
                out.retval = target_write_buffer(target, address, static_cast<uint32_t>(data.size()),
                                                  reinterpret_cast<const uint8_t*>(data.data()));
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during WriteMemory");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during WriteMemory");
    if (outcome->retval != ERROR_OK) return std::unexpected("target_write_buffer() failed");
    return {};
}

std::expected<void, std::string> OpenOcdProvider::RunTclCommand(const std::string& command) {
    struct command_context* cmd_ctx = impl_->cmd_ctx;

    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        int retval = ERROR_FAIL;
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([cmd_ctx, command]() {
        Outcome out;
        out.ok = RunGuarded([&]() { out.retval = command_run_line(cmd_ctx, const_cast<char*>(command.c_str())); },
                             &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during RunTclCommand");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during RunTclCommand");
    if (outcome->retval != ERROR_OK) return std::unexpected("'" + command + "' failed");
    return {};
}

std::expected<std::vector<TmcObject>, std::string> OpenOcdProvider::ListTraceSinks() {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        std::vector<TmcObject> result;
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([]() {
        Outcome out;
        out.ok = RunGuarded(
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
                    &out.result);
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during ListTraceSinks");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during ListTraceSinks");
    return outcome->result;
}

std::expected<std::vector<Etmv4Object>, std::string> OpenOcdProvider::ListTraceSources() {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        std::vector<Etmv4Object> result;
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([]() {
        Outcome out;
        out.ok = RunGuarded(
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
                    &out.result);
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during ListTraceSources");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during ListTraceSources");
    return outcome->result;
}

std::expected<model::Etmv4Registers, std::string> OpenOcdProvider::ReadETMv4Registers(const std::string& name) {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        std::string failure;
        model::Etmv4Registers regs{};
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([name]() {
        Outcome out;
        out.ok = RunGuarded([&]() {
                struct etmv4_object* obj = etmv4_find_by_name(name.c_str());
                if (!obj) {
                    out.failure = "no ETMv4 object named '" + name + "'";
                    return;
                }
                if (!etmv4_object_initialised(obj)) {
                    out.failure = "ETMv4 object '" + name + "' is not initialised";
                    return;
                }
                etmv4_decode_regs d_regs = etmv4_object_decode_regs(obj);
                out.regs.trcconfigr = d_regs.trcconfigr;
                out.regs.trctraceidr = d_regs.trctraceidr;
                out.regs.trcidr0 = d_regs.trcidr0;
                out.regs.trcidr1 = d_regs.trcidr1;
                out.regs.trcidr2 = d_regs.trcidr2;
                out.regs.trcidr8 = d_regs.trcidr8;
                out.regs.trcidr9 = d_regs.trcidr9;
                out.regs.trcidr10 = d_regs.trcidr10;
                out.regs.trcidr11 = d_regs.trcidr11;
                out.regs.trcidr12 = d_regs.trcidr12;
                out.regs.trcidr13 = d_regs.trcidr13;
                out.regs.trcidr3 = d_regs.trcidr3;
                out.regs.trcidr4 = d_regs.trcidr4;
                out.regs.trcidr5 = d_regs.trcidr5;
                out.regs.trcidr6 = d_regs.trcidr6;
                out.regs.trcidr7 = d_regs.trcidr7;
                out.regs.trcauthstatus = d_regs.trcauthstatus;
        },
        &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during ReadETMv4Registers");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during ReadETMv4Registers");
    if (!outcome->failure.empty()) return std::unexpected(outcome->failure);
    return outcome->regs;
}

std::expected<void, std::string> OpenOcdProvider::SubscribeTrace(const std::string& name, TraceDataCallback callback) {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        std::string failure;
    };
    Impl* impl = impl_.get();
    std::optional<Outcome> outcome = impl_->queue.RunSync([impl, name, callback = std::move(callback)]() mutable {
        Outcome out;
        out.ok = RunGuarded([&]() {
                struct tmc_object* obj = tmc_find_by_name(name.c_str());
                if (!obj) {
                    out.failure = "no TMC object named '" + name + "'";
                    return;
                }
                auto [it, inserted] = impl->trace_callbacks.insert_or_assign(name, std::move(callback));
                tmc_set_capture_callback(obj, &TraceCallbackTrampoline, &it->second);
        },
        &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during SubscribeTrace");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during SubscribeTrace");
    if (!outcome->failure.empty()) return std::unexpected(outcome->failure);
    return {};
}

std::expected<void, std::string> OpenOcdProvider::UnsubscribeTrace(const std::string& name) {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        std::string failure;
    };
    Impl* impl = impl_.get();
    std::optional<Outcome> outcome = impl_->queue.RunSync([impl, name]() {
        Outcome out;
        out.ok = RunGuarded([&]() {
                struct tmc_object* obj = tmc_find_by_name(name.c_str());
                if (!obj) {
                    out.failure = "no TMC object named '" + name + "'";
                    return;
                }
                tmc_clear_capture_callback(obj);
                impl->trace_callbacks.erase(name);
        },
        &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during UnsubscribeTrace");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during UnsubscribeTrace");
    if (!outcome->failure.empty()) return std::unexpected(outcome->failure);
    return {};
}

std::expected<void, std::string> OpenOcdProvider::SubscribeTargetState(TargetStateCallback callback) {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
    };
    Impl* impl = impl_.get();
    std::optional<Outcome> outcome = impl_->queue.RunSync([impl, callback = std::move(callback)]() mutable {
        Outcome out;
        out.ok = RunGuarded(
            [&]() {
                const bool already_registered = static_cast<bool>(impl->target_state_callback);
                impl->target_state_callback = std::move(callback);
                if (!already_registered)
                    target_register_event_callback(&TargetStateTrampoline, &impl->target_state_callback);
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during SubscribeTargetState");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during SubscribeTargetState");
    return {};
}

std::expected<void, std::string> OpenOcdProvider::UnsubscribeTargetState() {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
    };
    Impl* impl = impl_.get();
    std::optional<Outcome> outcome = impl_->queue.RunSync([impl]() {
        Outcome out;
        out.ok = RunGuarded(
            [&]() {
                target_unregister_event_callback(&TargetStateTrampoline, &impl->target_state_callback);
                impl->target_state_callback = nullptr;
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during UnsubscribeTargetState");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during UnsubscribeTargetState");
    return {};
}


std::expected<void, std::string> OpenOcdProvider::ConfigureTrace(const std::string& name, const std::string& options) {
    struct command_context* cmd_ctx = impl_->cmd_ctx;

    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        int retval = ERROR_FAIL;
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([cmd_ctx, name, options]() {
        Outcome out;
        out.ok = RunGuarded(
            [&]() {
                std::string line = name + " configure " + options;
                out.retval = command_run_line(cmd_ctx, const_cast<char*>(line.c_str()));
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during ConfigureTrace");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during ConfigureTrace");
    if (outcome->retval != ERROR_OK) return std::unexpected("'" + name + " configure " + options + "' failed");
    return {};
}

std::expected<void, std::string> OpenOcdProvider::EnableTrace(const std::string& name) {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        bool found = false;
        int retval = ERROR_OK;
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([name]() {
        Outcome out;
        out.ok = RunGuarded(
            [&]() {
                if (struct tmc_object* tmc = tmc_find_by_name(name.c_str())) {
                    out.found = true;
                    out.retval = tmc_enable(tmc);
                    return;
                }
                if (struct etmv4_object* etmv4 = etmv4_find_by_name(name.c_str())) {
                    out.found = true;
                    out.retval = etmv4_enable(etmv4);
                }
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during EnableTrace");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during EnableTrace");
    if (!outcome->found) return std::unexpected("no TMC or ETMv4 object named '" + name + "'");
    if (outcome->retval != ERROR_OK) return std::unexpected("'" + name + "' failed to enable");
    return {};
}

std::expected<void, std::string> OpenOcdProvider::DisableTrace(const std::string& name) {
    struct Outcome {
        bool ok = false;
        int exit_code = 0;
        bool found = false;
        int retval = ERROR_OK;
    };
    std::optional<Outcome> outcome = impl_->queue.RunSync([name]() {
        Outcome out;
        out.ok = RunGuarded(
            [&]() {
                if (struct tmc_object* tmc = tmc_find_by_name(name.c_str())) {
                    out.found = true;
                    out.retval = tmc_disable(tmc);
                    return;
                }
                if (struct etmv4_object* etmv4 = etmv4_find_by_name(name.c_str())) {
                    out.found = true;
                    out.retval = etmv4_disable(etmv4);
                }
            },
            &out.exit_code);
        return out;
    });
    if (!outcome) return std::unexpected("timed out waiting for OpenOCD during DisableTrace");
    if (!outcome->ok) return std::unexpected("openocd_exit(" + std::to_string(outcome->exit_code) + ") during DisableTrace");
    if (!outcome->found) return std::unexpected("no TMC or ETMv4 object named '" + name + "'");
    if (outcome->retval != ERROR_OK) return std::unexpected("'" + name + "' failed to disable");
    return {};
}

}  // namespace providers
