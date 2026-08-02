#ifndef TRAILER_CORE_COMPONENTS_DEVICE_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_DEVICE_MANAGER_HPP_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/event_bus.hpp"
#include "device_provider/memory_map.hpp"
#include "device_xml/svd_model.hpp"
#include "llvm/Support/Error.h"
#include "openocd_provider/memory_selector.hpp"

namespace core {

class DebugContext;

class DeviceManager {
public:
  explicit DeviceManager(DebugContext &context) : m_context(context) {}
  ~DeviceManager();

  DeviceManager(const DeviceManager &) = delete;
  DeviceManager &operator=(const DeviceManager &) = delete;

  // Index lookup, Rzone parse and the core (architecture) SVD are all small
  // and synchronous - the memory map has to exist before the first
  // setBreakpoints. Also starts the device SVD background parse (see
  // EnsureDevicePeripherals) so it has a head start, same as
  // DisassemblyManager::InvalidateProgram(). A no-op on an empty device_name.
  void Configure(const std::string &device_name, const std::string &svd_path);

  const providers::device::MemoryMap &Memory() const { return m_memory; }
  const std::optional<device_xml::Device> &CorePeripherals() const { return m_core; }

  // Starts (once) a background parse of the user-supplied device SVD - this
  // can be tens of MB, unlike the core SVD above - and returns a future
  // callers await only when they actually need peripheral data. Configure()
  // already triggers this; safe to call again, returns the same future.
  std::shared_future<void> EnsureDevicePeripherals();
  const std::optional<device_xml::Device> &DevicePeripherals() const { return m_device_peripherals; }
  const std::string &DevicePeripheralsError() const { return m_device_peripherals_error; }

  // core selects the bundled architecture SVD's peripheral list
  // (DeviceManager::CorePeripherals()) instead of the user-supplied device
  // SVD's.
  const device_xml::Peripheral *FindPeripheral(const std::string &name, bool core = false) const;

  llvm::Expected<std::vector<RegisterValue>> ReadPeripheral(const std::string &peripheral_name, bool safe_only,
                                                             bool core = false);

  // nullopt when the register isn't read-safe (write-only or has a read
  // side effect) - no read-back is attempted in that case.
  llvm::Expected<std::optional<std::uint32_t>> WritePeripheralRegister(const std::string &peripheral_name,
                                                                        const std::string &register_name,
                                                                        std::uint32_t value, bool core = false);

  struct PeripheralWatchStartArgs {
    std::string peripheral;
    std::uint64_t interval_ms = 500;
    bool safe_only = true;
    bool core = false;
  };

  llvm::Expected<int64_t> StartPeripheralWatch(const PeripheralWatchStartArgs &args);
  llvm::Error StopPeripheralWatch(int64_t watch_id);

  // Stops and joins every peripheral watch. Called from the destructor and
  // from TargetManager::Disconnect() before it tears down the
  // OpenOcdProvider a running watch worker would otherwise still read
  // through - same contract as WatchManager::StopAll().
  void StopAllPeripheralWatches();

private:
  struct RegisterRun {
    std::uint32_t start_offset = 0;
    std::uint32_t byte_length = 0;
    std::vector<const device_xml::Register *> registers;
  };

  struct PeripheralWatch {
    int64_t id = 0;
    std::string peripheral_name;
    std::uint32_t base_address = 0;
    std::vector<RegisterRun> runs;
    std::chrono::milliseconds interval{500};
    providers::MemorySelector selector;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    bool stopping = false;
    std::uint64_t sequence = 0;
    std::uint64_t epoch = 0;
    bool last_read_ok = true;
  };

  std::vector<RegisterRun> CoalesceSafeRuns(const device_xml::Peripheral &peripheral, bool safe_only) const;
  void PeripheralWatchWorkerMain(PeripheralWatch *watch);
  void BumpWatchEpochs(const std::string &peripheral_name);

  DebugContext &m_context;
  providers::device::MemoryMap m_memory;
  std::optional<device_xml::Device> m_core;

  std::string m_svd_path;
  std::mutex m_svd_mutex;
  std::optional<std::shared_future<void>> m_svd_future;
  std::optional<device_xml::Device> m_device_peripherals;
  std::string m_device_peripherals_error;

  std::mutex m_watch_mutex;
  int64_t m_next_watch_id = 1;
  std::unordered_map<int64_t, std::unique_ptr<PeripheralWatch>> m_watches;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_DEVICE_MANAGER_HPP_
