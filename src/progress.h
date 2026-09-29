// How a long computation -- a world build, the climate run inside it --
// reports what it is doing and is told to stop, without either end knowing
// about the other. Technical/Globe Viewer.md §Generation feedback describes
// the game's use: the build runs on a worker thread, reports into a slot the
// main thread reads, and is cancelled when the window closes (work order 07).
//
// The context is passed down the call chain rather than parked in a global,
// so two builds can run at once and a callee cannot report to a caller that
// has already returned.
#pragma once
#include <atomic>
#include <functional>
#include <utility>

namespace progress {

struct Context {
    // Called with each stage's label, and with "" when the build is done.
    // Empty: nobody is listening. Runs on the thread doing the work, so it
    // must not touch another thread's state unsynchronised.
    std::function<void(const char* stage)> report;
    // Set from any thread to stop the work at its next check. Null: the work
    // cannot be stopped.
    const std::atomic<bool>* cancel = nullptr;

    Context() = default;
    // A probe's plain function, or nullptr for none: implicit, so a probe
    // passes its callback exactly as it did before there was a context.
    Context(void (*fn)(const char* stage)) {
        if (fn) report = fn;
    }
    Context(std::function<void(const char* stage)> fn, const std::atomic<bool>* stop)
        : report(std::move(fn)), cancel(stop) {}

    void say(const char* stage) const {
        if (report) report(stage);
    }
    bool cancelled() const { return cancel && cancel->load(std::memory_order_relaxed); }
};

} // namespace progress
