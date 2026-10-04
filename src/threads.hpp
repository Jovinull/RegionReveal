#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rr {

// The process's main thread: the one created first. It is the thread that will
// draw the map, whether the mod was loaded by the game's import table (then it
// is the current thread) or injected by a loader (then it is not).
unsigned long main_thread_id();

// Suspends every other thread of the process while code is patched, so none of
// them can run half-written instructions, and resumes them on destruction.
//
// A thread stopped inside one of `guarded` - with its instruction pointer past
// the start of an instruction about to be replaced - is let go and tried again,
// for at most a second. ok() says whether a clean stop was reached.
class ThreadFreeze {
public:
    struct Range {
        const std::uint8_t* begin;
        std::size_t size;
    };

    explicit ThreadFreeze(const std::vector<Range>& guarded);
    ~ThreadFreeze();
    ThreadFreeze(const ThreadFreeze&) = delete;
    ThreadFreeze& operator=(const ThreadFreeze&) = delete;

    bool ok() const { return ok_; }

private:
    bool suspend_all(const std::vector<Range>& guarded);
    void resume_all();

    std::vector<void*> suspended_;
    bool ok_ = false;
};

}  // namespace rr
