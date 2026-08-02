#include "core/components/device_manager.hpp"

#include <algorithm>
#include <utility>

#include "core/components/memory_manager.hpp"
#include "core/debug_context.hpp"
#include "core/event_bus.hpp"
#include "dap/dap_error.hpp"
#include "device_provider/device_pack.hpp"
#include "openocd_provider/openocd_provider.hpp"

namespace core {

namespace {

std::uint32_t DecodeLE(const std::byte *data, std::uint32_t size_bits) {
  std::uint32_t value = 0;
  const std::uint32_t bytes = size_bits / 8;
  for (std::uint32_t i = 0; i < bytes; ++i)
    value |= static_cast<std::uint32_t>(data[i]) << (8 * i);
  return value;
}

std::vector<char> EncodeLE(std::uint32_t value, std::uint32_t size_bits) {
  const std::uint32_t bytes = size_bits / 8;
  std::vector<char> out(bytes);
  for (std::uint32_t i = 0; i < bytes; ++i)
    out[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
  return out;
}

std::vector<std::byte> ToStdBytes(const std::vector<char> &data) {
  std::vector<std::byte> out(data.size());
  for (std::size_t i = 0; i < data.size(); ++i)
    out[i] = static_cast<std::byte>(data[i]);
  return out;
}

}  // namespace

DeviceManager::~DeviceManager() { StopAllPeripheralWatches(); }

void DeviceManager::Configure(const std::string &device_name, const std::string &svd_path) {
  m_svd_path = svd_path;
  EnsureDevicePeripherals();
  if (device_name.empty()) return;

  providers::device::DevicePack pack = providers::device::DevicePack::Load(device_name);
  m_memory = pack.Memory();
  m_core = pack.CorePeripherals();
}

std::shared_future<void> DeviceManager::EnsureDevicePeripherals() {
  std::lock_guard<std::mutex> lock(m_svd_mutex);
  if (!m_svd_future) {
    m_svd_future = std::async(std::launch::async, [this]() {
                     // No svdPath configured isn't an error - it just means
                     // the caller never asked for a peripheral view.
                     if (m_svd_path.empty()) return;
                     std::expected<device_xml::Device, std::string> result = providers::device::LoadDeviceSvd(m_svd_path);
                     std::lock_guard<std::mutex> inner_lock(m_svd_mutex);
                     if (result) {
                       m_device_peripherals = std::move(*result);
                     } else {
                       m_device_peripherals_error = result.error();
                     }
                   }).share();
  }
  return *m_svd_future;
}

const device_xml::Peripheral *DeviceManager::FindPeripheral(const std::string &name, bool core) const {
  const std::optional<device_xml::Device> &device = core ? m_core : m_device_peripherals;
  if (!device) return nullptr;
  for (const device_xml::Peripheral &peripheral : device->peripherals) {
    if (peripheral.name == name) return &peripheral;
  }
  return nullptr;
}

std::vector<DeviceManager::RegisterRun> DeviceManager::CoalesceSafeRuns(const device_xml::Peripheral &peripheral,
                                                                         bool safe_only) const {
  std::vector<const device_xml::Register *> candidates;
  for (const device_xml::Register &reg : peripheral.registers) {
    if (!safe_only || reg.ReadSafe()) candidates.push_back(&reg);
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const device_xml::Register *a, const device_xml::Register *b) {
              return a->address_offset < b->address_offset;
            });

  std::vector<RegisterRun> runs;
  for (const device_xml::Register *reg : candidates) {
    const std::uint32_t reg_bytes = reg->size_bits / 8;
    if (!runs.empty()) {
      RegisterRun &last = runs.back();
      const std::uint32_t last_end = last.start_offset + last.byte_length;
      // Only merge same-width adjacent registers - AHB peripherals can
      // fault on an access width that doesn't match what a register
      // expects, so a run never spans a width change.
      if (reg->address_offset == last_end && reg->size_bits == last.registers.back()->size_bits) {
        last.byte_length += reg_bytes;
        last.registers.push_back(reg);
        continue;
      }
    }
    RegisterRun run;
    run.start_offset = reg->address_offset;
    run.byte_length = reg_bytes;
    run.registers.push_back(reg);
    runs.push_back(std::move(run));
  }
  return runs;
}

llvm::Expected<std::vector<RegisterValue>> DeviceManager::ReadPeripheral(const std::string &peripheral_name,
                                                                          bool safe_only, bool core) {
  const device_xml::Peripheral *peripheral = FindPeripheral(peripheral_name, core);
  if (!peripheral)
    return llvm::make_error<dap::DAPError>("no such peripheral: " + peripheral_name);

  std::vector<RegisterRun> runs = CoalesceSafeRuns(*peripheral, safe_only);
  std::vector<RegisterValue> values;
  for (const RegisterRun &run : runs) {
    llvm::Expected<MemoryReadResult> result =
        m_context.Memory().ReadMemory(peripheral->base_address + run.start_offset, run.byte_length);
    if (!result) return result.takeError();

    std::size_t offset = 0;
    for (const device_xml::Register *reg : run.registers) {
      values.push_back({reg->name, DecodeLE(result->data.data() + offset, reg->size_bits)});
      offset += reg->size_bits / 8;
    }
  }
  return values;
}

llvm::Expected<std::optional<std::uint32_t>> DeviceManager::WritePeripheralRegister(
    const std::string &peripheral_name, const std::string &register_name, std::uint32_t value, bool core) {
  const device_xml::Peripheral *peripheral = FindPeripheral(peripheral_name, core);
  if (!peripheral)
    return llvm::make_error<dap::DAPError>("no such peripheral: " + peripheral_name);

  const device_xml::Register *reg = nullptr;
  for (const device_xml::Register &candidate : peripheral->registers) {
    if (candidate.name == register_name) {
      reg = &candidate;
      break;
    }
  }
  if (!reg)
    return llvm::make_error<dap::DAPError>("no such register: " + peripheral_name + "::" + register_name);

  const std::vector<char> encoded = EncodeLE(value, reg->size_bits);
  llvm::Expected<MemoryWriteResult> write =
      m_context.Memory().WriteMemory(peripheral->base_address + reg->address_offset, encoded, /*allow_partial=*/false);
  if (!write) return write.takeError();

  BumpWatchEpochs(peripheral_name);

  if (!reg->ReadSafe()) return std::optional<std::uint32_t>(std::nullopt);

  llvm::Expected<MemoryReadResult> read =
      m_context.Memory().ReadMemory(peripheral->base_address + reg->address_offset, reg->size_bits / 8);
  if (!read) return read.takeError();
  return std::optional<std::uint32_t>(DecodeLE(read->data.data(), reg->size_bits));
}

void DeviceManager::BumpWatchEpochs(const std::string &peripheral_name) {
  std::lock_guard<std::mutex> lock(m_watch_mutex);
  for (auto &[id, watch] : m_watches) {
    if (watch->peripheral_name != peripheral_name) continue;
    std::lock_guard<std::mutex> watch_lock(watch->mutex);
    ++watch->epoch;
  }
}

llvm::Expected<int64_t> DeviceManager::StartPeripheralWatch(const PeripheralWatchStartArgs &args) {
  if (!m_context.OpenOcd())
    return llvm::make_error<dap::DAPError>("no active OpenOCD session to watch peripherals on");

  const device_xml::Peripheral *peripheral = FindPeripheral(args.peripheral, args.core);
  if (!peripheral)
    return llvm::make_error<dap::DAPError>("no such peripheral: " + args.peripheral);

  auto watch = std::make_unique<PeripheralWatch>();
  watch->peripheral_name = args.peripheral;
  watch->base_address = peripheral->base_address;
  watch->runs = CoalesceSafeRuns(*peripheral, args.safe_only);
  watch->interval = std::chrono::milliseconds(args.interval_ms == 0 ? 1 : args.interval_ms);

  PeripheralWatch *raw = watch.get();
  int64_t id;
  {
    std::lock_guard<std::mutex> lock(m_watch_mutex);
    id = m_next_watch_id++;
    raw->id = id;
    m_watches.emplace(id, std::move(watch));
  }
  raw->worker = std::thread(&DeviceManager::PeripheralWatchWorkerMain, this, raw);
  return id;
}

llvm::Error DeviceManager::StopPeripheralWatch(int64_t watch_id) {
  std::unique_ptr<PeripheralWatch> watch;
  {
    std::lock_guard<std::mutex> lock(m_watch_mutex);
    auto it = m_watches.find(watch_id);
    if (it == m_watches.end())
      return llvm::make_error<dap::DAPError>("no such peripheral watch");
    watch = std::move(it->second);
    m_watches.erase(it);
  }

  {
    std::lock_guard<std::mutex> lock(watch->mutex);
    watch->stopping = true;
  }
  watch->cv.notify_one();
  watch->worker.join();
  return llvm::Error::success();
}

void DeviceManager::StopAllPeripheralWatches() {
  std::unordered_map<int64_t, std::unique_ptr<PeripheralWatch>> watches;
  {
    std::lock_guard<std::mutex> lock(m_watch_mutex);
    watches = std::move(m_watches);
  }
  for (auto &[id, watch] : watches) {
    std::lock_guard<std::mutex> lock(watch->mutex);
    watch->stopping = true;
  }
  for (auto &[id, watch] : watches) {
    watch->cv.notify_one();
    if (watch->worker.joinable()) watch->worker.join();
  }
}

void DeviceManager::PeripheralWatchWorkerMain(PeripheralWatch *watch) {
  auto next_deadline = std::chrono::steady_clock::now();
  std::unique_lock<std::mutex> lock(watch->mutex);
  while (!watch->stopping) {
    const std::uint64_t captured_epoch = watch->epoch;
    lock.unlock();

    std::vector<RegisterValue> values;
    std::expected<void, std::string> failure;
    bool ok = true;
    if (providers::OpenOcdProvider *openocd = m_context.OpenOcd()) {
      for (const RegisterRun &run : watch->runs) {
        std::expected<std::vector<std::byte>, std::string> result =
            openocd->ReadMemory(watch->selector, watch->base_address + run.start_offset, run.byte_length);
        if (!result) {
          ok = false;
          failure = std::unexpected(result.error());
          break;
        }
        std::size_t offset = 0;
        for (const device_xml::Register *reg : run.registers) {
          values.push_back({reg->name, DecodeLE(result->data() + offset, reg->size_bits)});
          offset += reg->size_bits / 8;
        }
      }
    } else {
      ok = false;
      failure = std::unexpected("no active OpenOCD session");
    }

    lock.lock();
    if (watch->stopping) break;

    if (ok) {
      if (!watch->last_read_ok) {
        watch->last_read_ok = true;
        lock.unlock();
        m_context.Emit(PeripheralWatchStateEvent{watch->id, /*active=*/true, ""});
        lock.lock();
      }
      // A write to this peripheral bumped the epoch while this read was in
      // flight (or before it started) - the data may predate that write,
      // so it's dropped rather than shown as current.
      if (watch->epoch == captured_epoch) {
        PeripheralWatchDataEvent event;
        event.watch_id = watch->id;
        event.sequence = watch->sequence++;
        event.epoch = watch->epoch;
        event.registers = std::move(values);
        lock.unlock();
        m_context.Emit(std::move(event));
        lock.lock();
      }
    } else if (watch->last_read_ok) {
      watch->last_read_ok = false;
      lock.unlock();
      m_context.Emit(PeripheralWatchStateEvent{watch->id, /*active=*/false, failure.error()});
      lock.lock();
    }

    const auto now = std::chrono::steady_clock::now();
    next_deadline += watch->interval;
    if (next_deadline < now) next_deadline = now + watch->interval;
    watch->cv.wait_until(lock, next_deadline, [watch] { return watch->stopping; });
  }
}

}  // namespace core
