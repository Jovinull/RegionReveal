// Starts Cube.exe suspended, injects RegionReveal.dll, then lets it run.
//
// Nothing on disk is modified: the game executable, its DLLs and the save files
// are all left exactly as they are. Remove the launcher and the install is
// vanilla again.
//
// Usage: RegionRevealLauncher.exe [path\to\Cube.exe]
// Defaults to Cube.exe beside the launcher.

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

std::wstring directory_of(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring beside_launcher(const wchar_t* name) {
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return directory_of(self) + L'\\' + name;
}

bool inject(HANDLE process, const std::wstring& dll) {
    // LoadLibraryW keeps the path wide, so an install under a non-ASCII path
    // still injects. kernel32 is loaded at the same address in both processes.
    const std::size_t bytes = (dll.size() + 1) * sizeof(wchar_t);

    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;

    if (!WriteProcessMemory(process, remote, dll.c_str(), bytes, nullptr)) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    auto* load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
    if (!thread) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(thread, INFINITE);
    DWORD loaded = 0;
    GetExitCodeThread(thread, &loaded);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return loaded != 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring game = argc > 1 ? argv[1] : beside_launcher(L"Cube.exe");
    const std::wstring dll = beside_launcher(L"RegionReveal.dll");

    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"RegionReveal.dll not found next to the launcher\n");
        return 1;
    }

    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    std::wstring command = L'"' + game + L'"';
    const std::wstring working = directory_of(game);

    if (!CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                        nullptr, working.c_str(), &startup, &info)) {
        std::fwprintf(stderr, L"could not start %ls (error %lu)\n", game.c_str(), GetLastError());
        return 1;
    }

    if (!inject(info.hProcess, dll)) {
        std::fwprintf(stderr, L"injection failed; terminating the game\n");
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        return 1;
    }

    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return 0;
}
