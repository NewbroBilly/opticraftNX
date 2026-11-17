#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace opti::runtime {

// Deterministic cooperative scheduler. Each Java-thread adapter supplies a
// bounded step callback; callbacks must yield rather than block the Switch OS.
class JavaThreadScheduler {
public:
    using ThreadId = std::uint32_t;
    enum class State { New, Runnable, Running, Parked, Terminated, Failed };
    struct Snapshot { ThreadId id; std::string name; State state; std::uint64_t steps; };
    using Step = std::function<bool()>; // true = yielded/continue, false = completed

    explicit JavaThreadScheduler(std::size_t max_threads = 256) : max_threads_(max_threads) {}
    ThreadId create(std::string name, Step step, bool daemon = false);
    bool start(ThreadId id);
    std::size_t run_ready(std::size_t max_steps);
    bool interrupt(ThreadId id);
    bool park(ThreadId id);
    bool resume(ThreadId id);
    bool interrupted(ThreadId id, bool clear = false);
    bool is_alive(ThreadId id) const;
    State state(ThreadId id) const;
    std::vector<Snapshot> snapshot() const;
    std::size_t size() const { return threads_.size(); }

private:
    struct Thread { ThreadId id; std::string name; Step step; State state=State::New; bool daemon=false; bool interrupted=false; std::uint64_t steps=0; };
    Thread* find(ThreadId id);
    const Thread* find(ThreadId id) const;
    std::vector<Thread> threads_;
    std::size_t max_threads_;
    ThreadId next_id_=1;
    std::size_t cursor_=0;
};

} // namespace opti::runtime
