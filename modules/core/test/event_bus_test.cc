// Synthetic race test for core::EventBus: Publish() is called concurrently
// from multiple threads, the way it really is (dispatch thread, LLDB event
// thread, OpenOCD server_loop thread). The per-publish counter below is
// deliberately not atomic - it is only safe because EventBus::Publish()
// serializes subscriber execution under its own mutex. Run under
// ThreadSanitizer: a race here means that lock isn't doing its job.

#include "core/event_bus.hpp"

#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {

constexpr int kThreads = 3;
constexpr int kPublishesPerThread = 2000;

int Fail(const std::string &message) {
    std::cerr << "event_bus_test: " << message << "\n";
    return EXIT_FAILURE;
}

}  // namespace

int main() {
    core::EventBus bus;

    size_t delivered = 0;
    bus.Subscribe([&](const core::DomainEvent &) { ++delivered; });

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kPublishesPerThread; ++i)
                bus.Publish(core::OutputEvent{core::OutputCategory::Console, "tick"});
        });
    }
    for (std::thread &thread : threads) thread.join();

    const size_t expected = static_cast<size_t>(kThreads) * kPublishesPerThread;
    if (delivered != expected)
        return Fail("expected " + std::to_string(expected) + " deliveries, got " + std::to_string(delivered));

    std::cout << "event_bus_test: ok (" << delivered << " deliveries across " << kThreads << " threads)\n";
    return EXIT_SUCCESS;
}
