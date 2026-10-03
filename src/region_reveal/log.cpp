#include "log.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

#include "paths.hpp"

namespace rr {

void log_line(const char* message) {
    static const std::wstring path = beside_game(L"RegionReveal.log");
    if (path.empty()) return;

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"a") != 0 || !file) return;

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
