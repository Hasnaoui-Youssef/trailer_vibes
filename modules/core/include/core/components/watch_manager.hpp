#ifndef TRAILER_CORE_COMPONENTS_WATCH_MANAGER_HPP_
#define TRAILER_CORE_COMPONENTS_WATCH_MANAGER_HPP_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"
#include "llvm/Support/Error.h"
#include "openocd_provider/memory_selector.hpp"

namespace core {

class DebugContext;

// A live memory watch is a periodic read pushed to the client as
// WatchDataEvents, independent of LLDB's stopped/running state - reads go
// straight through OpenOCD (see WorkerMain), the same as
// OpenOcdMemoryStrategy. Some peripherals keep running under debug halt
// (e.g. an STM32 DBGMCU freeze bit left unset), so a watch is never paused
// on halt - only Stop()/StopAll() end it.
//
// One worker thread per watch. Watch counts are small (a handful of live
// memory views at most), so a thread per watch is simple and sufficient -
// see the live memory watches plan for why this doesn't need a shared pool.
class WatchManager {
public:
  explicit WatchManager(DebugContext &context) : m_context(context) {}
  ~WatchManager();

  WatchManager(const WatchManager &) = delete;
  WatchManager &operator=(const WatchManager &) = delete;

  struct StartArgs {
    lldb::addr_t address = LLDB_INVALID_ADDRESS;
    uint64_t size = 0;
    uint64_t interval_ms = 500;
    std::string target_name;
  };

  llvm::Expected<int64_t> Start(const StartArgs &args);
  llvm::Error Stop(int64_t watch_id);

  // Stops and joins every watch. Called from the destructor, and from
  // TargetManager::Disconnect() before it tears down the OpenOcdProvider a
  // running watch worker would otherwise still be reading through.
  void StopAll();

private:
  struct Watch {
    int64_t id = 0;
    lldb::addr_t address = LLDB_INVALID_ADDRESS;
    uint64_t size = 0;
    std::chrono::milliseconds interval{500};
    providers::MemorySelector selector;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    bool stopping = false;
    uint64_t sequence = 0;
    bool last_read_ok = true;
  };

  void WorkerMain(Watch *watch);

  DebugContext &m_context;
  std::mutex m_mutex;
  int64_t m_next_id = 1;
  std::unordered_map<int64_t, std::unique_ptr<Watch>> m_watches;
};

}  // namespace core

#endif  // TRAILER_CORE_COMPONENTS_WATCH_MANAGER_HPP_
