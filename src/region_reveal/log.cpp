#include "log.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace rr {

void log_line(const char* message) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;

    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"RegionReveal.log");

    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"a") != 0 || !file) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::fprintf(file, "[%02u:%02u:%02u] %s\n", now.wHour, now.wMinute, now.wSecond, message);
    std::fclose(file);
}

void log_linef(const char* format, ...) {
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    va_end(args);
    log_line(message);
}

}  // namespace rr
