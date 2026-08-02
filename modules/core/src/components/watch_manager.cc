#include "core/components/watch_manager.hpp"

#include <cstddef>
#include <expected>
#include <utility>
#include <vector>

#include "core/debug_context.hpp"
#include "dap/dap_error.hpp"
#include "openocd_provider/openocd_provider.hpp"

namespace core {

WatchManager::~WatchManager() { StopAll(); }

llvm::Expected<int64_t> WatchManager::Start(const StartArgs &args) {
  if (!m_context.OpenOcd())
    return llvm::make_error<dap::DAPError>("no active OpenOCD session to watch memory on");
  if (args.size == 0)
    return llvm::make_error<dap::DAPError>("watch size must be greater than zero");

  auto watch = std::make_unique<Watch>();
  watch->address = args.address;
  watch->size = args.size;
  watch->interval = std::chrono::milliseconds(args.interval_ms == 0 ? 1 : args.interval_ms);
  watch->selector.target_name = args.target_name;

  Watch *raw = watch.get();
  int64_t id;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    id = m_next_id++;
    raw->id = id;
    m_watches.emplace(id, std::move(watch));
  }
  raw->worker = std::thread(&WatchManager::WorkerMain, this, raw);
  return id;
}

llvm::Error WatchManager::Stop(int64_t watch_id) {
  std::unique_ptr<Watch> watch;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_watches.find(watch_id);
    if (it == m_watches.end())
      return llvm::make_error<dap::DAPError>("no such watch");
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

void WatchManager::StopAll() {
  std::unordered_map<int64_t, std::unique_ptr<Watch>> watches;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    watches = std::move(m_watches);
  }
  for (auto &[id, watch] : watches) {
    std::lock_guard<std::mutex> lock(watch->mutex);
    watch->stopping = true;
  }
  for (auto &[id, watch] : watches) {
    watch->cv.notify_one();
    if (watch->worker.joinable())
      watch->worker.join();
  }
}

void WatchManager::WorkerMain(Watch *watch) {
  auto next_deadline = std::chrono::steady_clock::now();
  std::unique_lock<std::mutex> lock(watch->mutex);
  while (!watch->stopping) {
    lock.unlock();

    std::expected<std::vector<std::byte>, std::string> result;
    if (providers::OpenOcdProvider *openocd = m_context.OpenOcd())
      result = openocd->ReadMemory(watch->selector, watch->address, static_cast<uint32_t>(watch->size));
    else
      result = std::unexpected("no active OpenOCD session");

    lock.lock();
    if (watch->stopping) break;

    if (result) {
      if (!watch->last_read_ok) {
        watch->last_read_ok = true;
        lock.unlock();
        m_context.Emit(WatchStateEvent{watch->id, /*active=*/true, ""});
        lock.lock();
      }
      WatchDataEvent event;
      event.watch_id = watch->id;
      event.address = watch->address;
      event.sequence = watch->sequence++;
      event.data = std::move(*result);
      lock.unlock();
      m_context.Emit(std::move(event));
      lock.lock();
    } else if (watch->last_read_ok) {
      watch->last_read_ok = false;
      lock.unlock();
      m_context.Emit(WatchStateEvent{watch->id, /*active=*/false, result.error()});
      lock.lock();
    }

    const auto now = std::chrono::steady_clock::now();
    next_deadline += watch->interval;
    if (next_deadline < now)
      next_deadline = now + watch->interval;
    watch->cv.wait_until(lock, next_deadline, [watch] { return watch->stopping; });
  }
}

}  // namespace core
