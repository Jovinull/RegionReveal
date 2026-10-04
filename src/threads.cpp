#include "threads.hpp"

#include <windows.h>
#include <tlhelp32.h>

namespace rr {
namespace {

template <typename Visit>
void for_each_thread(Visit visit) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    const DWORD self = GetCurrentProcessId();
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == self) visit(entry.th32ThreadID);
    }
    CloseHandle(snapshot);
}

}  // namespace

unsigned long main_thread_id() {
    DWORD best = GetCurrentThreadId();
    ULONGLONG earliest = ~0ull;
    for_each_thread([&](DWORD id) {
        HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
        if (!thread) return;
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
            const ULONGLONG at = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            if (at < earliest) {
                earliest = at;
                best = id;
            }
        }
        CloseHandle(thread);
    });
    return best;
}

ThreadFreeze::ThreadFreeze(const std::vector<Range>& guarded) {
    for (int attempt = 0; attempt < 100 && !ok_; ++attempt) {
        ok_ = suspend_all(guarded);
        if (!ok_) {
            resume_all();
            Sleep(10);
        }
    }
}

ThreadFreeze::~ThreadFreeze() { resume_all(); }

bool ThreadFreeze::suspend_all(const std::vector<Range>& guarded) {
    // Everything that allocates happens before the first thread is stopped: a
    // stopped thread may be holding the heap lock.
    std::vector<DWORD> ids;
    const DWORD self = GetCurrentThreadId();
    for_each_thread([&](DWORD id) {
        if (id != self) ids.push_back(id);
    });
    suspended_.reserve(ids.size());

    bool clear = true;
    for (const DWORD id : ids) {
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, id);
        if (!thread) continue;  // already gone
        if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
            CloseHandle(thread);
            continue;
        }
        suspended_.push_back(thread);

        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(thread, &context)) continue;
        const auto* ip = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(context.Eip));
        for (const Range& r : guarded) {
            // Stopped at the first byte is fine: the whole instruction is
            // replaced before the thread runs again. Inside is not.
            if (ip > r.begin && ip < r.begin + r.size) clear = false;
        }
    }
    return clear;
}

void ThreadFreeze::resume_all() {
    for (void* thread : suspended_) {
        ResumeThread(thread);
        CloseHandle(thread);
    }
    suspended_.clear();
}

}  // namespace rr
