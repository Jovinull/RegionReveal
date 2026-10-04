// Test H, run for real: dinput8.dll loaded into a process that is not Cube.exe.
//
// The mod must find nothing to hook, say so in RegionReveal.log, and still
// forward DirectInput8Create to the system's dinput8.dll - a player who puts
// the DLL beside the wrong game must get a working game. Also checks that
// ThreadFreeze really holds other threads, since the mod-loader build relies on
// it while it rewrites code.
//
//   host_test   (run from the folder that holds dinput8.dll)

#include <windows.h>
#include <unknwn.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "../src/threads.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) ++g_failures;
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

std::wstring beside_exe(const wchar_t* name) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    return s.substr(0, s.find_last_of(L'\\') + 1) + name;
}

std::string read_file(const std::wstring& path) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) return {};
    std::string out;
    char buffer[4096];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) out.append(buffer, n);
    std::fclose(file);
    return out;
}

// IID_IDirectInput8W, written out so the test needs no DirectX SDK.
const GUID kDirectInput8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};

void unsupported_host() {
    std::printf("dinput8.dll in a process that is not Cube.exe\n");
    const std::wstring log = beside_exe(L"RegionReveal.log");
    DeleteFileW(log.c_str());

    HMODULE dll = LoadLibraryW(beside_exe(L"dinput8.dll").c_str());
    check(dll != nullptr, "the DLL loads");
    if (!dll) return;

    const std::string text = read_file(log);
    check(text.find("RegionReveal loaded") != std::string::npos, "the log says it loaded");
    check(text.find("unsupported Cube World Alpha build - no hook installed") != std::string::npos,
          "and that this is not a supported build, so nothing was hooked");
    check(text.find("RegionReveal active") == std::string::npos, "it never claims to be active");

    using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll, "DirectInput8Create"));
    check(create != nullptr, "DirectInput8Create is exported");
    if (!create) return;
    IUnknown* input = nullptr;
    const HRESULT result = create(GetModuleHandleW(nullptr), 0x0800, kDirectInput8W,
                                  reinterpret_cast<void**>(&input), nullptr);
    check(SUCCEEDED(result) && input, "and forwards to the system's dinput8.dll, which creates DirectInput");
    if (input) input->Release();
}

std::atomic<long> g_counter{0};
std::atomic<bool> g_stop{false};

DWORD WINAPI spin(LPVOID) {
    while (!g_stop.load()) g_counter.fetch_add(1);
    return 0;
}

void thread_freeze() {
    std::printf("ThreadFreeze\n");
    HANDLE worker = CreateThread(nullptr, 0, spin, nullptr, 0, nullptr);
    Sleep(50);
    check(rr::main_thread_id() == GetCurrentThreadId(), "the main thread is the oldest thread");
    {
        rr::ThreadFreeze freeze({});
        check(freeze.ok(), "other threads can be stopped");
        const long before = g_counter.load();
        Sleep(50);
        check(g_counter.load() == before, "a stopped thread does not run");
    }
    const long after = g_counter.load();
    Sleep(50);
    check(g_counter.load() != after, "and runs again afterwards");
    g_stop = true;
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
}

}  // namespace

int main() {
    unsupported_host();
    thread_freeze();
    std::printf("\n%s\n", g_failures ? "FAILURES" : "all host tests passed");
    return g_failures == 0 ? 0 : 1;
}
