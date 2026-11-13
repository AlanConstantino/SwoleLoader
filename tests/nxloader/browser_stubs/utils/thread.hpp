#pragma once
#include <functional>
#include <thread>

namespace sphaira::utils {
struct Async {
    inline static bool fail_to_start{};
    std::thread worker;
    explicit Async(std::function<void()>&& callback) {
        if (!fail_to_start) {
            worker = std::thread(std::move(callback));
        }
    }
    ~Async() { if (worker.joinable()) worker.join(); }
    bool IsRunning() const { return worker.joinable(); }
};
}
