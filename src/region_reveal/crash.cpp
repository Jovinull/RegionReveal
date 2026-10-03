#include "crash.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "log.hpp"

#ifdef REGIONREVEAL_CRASHDUMP
#include <dbghelp.h>
#endif

namespace rr {
namespace {

std::atomic<bool> g_reported{false};
LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;

// "Cube.exe+0x202440", rebased to the module's preferred ImageBase so it lines
// up with the addresses in docs/; "?" when no module contains it.
void describe(const void* address, char* out, std::size_t size) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module)) {
        _snprintf_s(out, size, _TRUNCATE, "%p", address);
        return;
    }
    char path[MAX_PATH] = "?";
    GetModuleFileNameA(module, path, MAX_PATH);
    const char* name = std::strrchr(path, '\\');
    name = name ? name + 1 : path;

    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const std::uintptr_t preferred = nt->OptionalHeader.ImageBase;
    const std::uintptr_t offset = static_cast<const std::uint8_t*>(address) - base;
    _snprintf_s(out, size, _TRUNCATE, "%s!%08X", name, static_cast<unsigned>(preferred + offset));
}

bool in_code(std::uintptr_t value) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(value), &info, sizeof(info)) != sizeof(info)) return false;
    return info.State == MEM_COMMIT && info.Type == MEM_IMAGE &&
           (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
}

#ifdef REGIONREVEAL_CRASHDUMP
void write_dump(EXCEPTION_POINTERS* info) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"RegionReveal_crash.dmp");

    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (!dbghelp) return;
    using WriteFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                  PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    auto write = reinterpret_cast<WriteFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (write && file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        write(GetCurrentProcess(), GetCurrentProcessId(), file,
              static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                         MiniDumpWithDataSegs),
              &exception, nullptr, nullptr);
        log_line("crash: minidump written to RegionReveal_crash.dmp");
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}
#endif

void report(EXCEPTION_POINTERS* info, const char* how) {
    if (g_reported.exchange(true)) return;
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const DWORD thread = GetCurrentThreadId();

    char where[160];
    describe(record->ExceptionAddress, where, sizeof(where));
    log_linef("CRASH (%s) code %08lX at %s, thread %lu, accessing %p", how, record->ExceptionCode, where, thread,
              record->NumberParameters >= 2 ? reinterpret_cast<void*>(record->ExceptionInformation[1]) : nullptr);

    // Return addresses: every stack dword that points into executable image
    // code. Some are stale, but the first few are usually the real call chain.
    const auto* stack = reinterpret_cast<const std::uintptr_t*>(info->ContextRecord->Esp);
    int found = 0;
    for (int i = 0; i < 512 && found < 16; ++i) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(stack + i, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT) break;
        if (!in_code(stack[i])) continue;
        describe(reinterpret_cast<const void*>(stack[i]), where, sizeof(where));
        log_linef("  stack[%d] %s", i, where);
        ++found;
    }
#ifdef REGIONREVEAL_CRASHDUMP
    write_dump(info);
#endif
}

LONG WINAPI on_unhandled(EXCEPTION_POINTERS* info) {
    report(info, "unhandled");
    return g_previous ? g_previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

#ifdef REGIONREVEAL_CRASHDUMP
// First chance, but only for faults inside the heap code: that is where the
// last crash surfaced, and by the time a filter runs the heap may be too broken
// to write anything.
LONG CALLBACK on_first_chance(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != 0xC0000374) return EXCEPTION_CONTINUE_SEARCH;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &module);
    if (code == 0xC0000374 || (module && module == ntdll)) report(info, "first chance");
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

}  // namespace

void install_crash_reporter() {
    g_previous = SetUnhandledExceptionFilter(&on_unhandled);
#ifdef REGIONREVEAL_CRASHDUMP
    AddVectoredExceptionHandler(1, &on_first_chance);
#endif
}

}  // namespace rr
