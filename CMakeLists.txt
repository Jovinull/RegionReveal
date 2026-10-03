cmake_minimum_required(VERSION 3.20)
project(RegionReveal CXX)

# Cube World Alpha is a 32-bit MSVC 2012 binary, so the mod has to be a 32-bit
# DLL. Configure with: cmake -B build -A Win32
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "RegionReveal must be built for 32-bit; configure with -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(REGIONREVEAL_SOURCES
  src/mod.cpp
  src/hooks.cpp
  src/game/session.cpp
  src/game/signatures.cpp
  src/game/world.cpp
  src/region_reveal/areas.cpp
  src/region_reveal/crash.cpp
  src/region_reveal/log.cpp
  src/region_reveal/preview.cpp
  src/region_reveal/preview_tile.cpp
  src/region_reveal/reveal.cpp
  src/region_reveal/settings.cpp
  src/region_reveal/visited.cpp
)

# Writes RegionReveal_crash.dmp beside the game on a crash, for a debugger.
# Off by default: a dump is large and the log line already names the fault.
option(REGIONREVEAL_CRASHDUMP "Write a minidump beside the game on a crash" OFF)

# Preferred loader: Cube.exe imports one function from dinput8.dll and the
# loader searches its own folder first, so dropping this beside the game loads
# the mod without an injector. Only adds a file; nothing is renamed.
add_library(dinput8 SHARED ${REGIONREVEAL_SOURCES} src/loader/proxy_dinput8.cpp)
target_include_directories(dinput8 PRIVATE src)
target_compile_definitions(dinput8 PRIVATE REGIONREVEAL_PROXY_DINPUT8)
set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")

# Same mod without the proxy export, for use with an external injector.
add_library(RegionReveal SHARED ${REGIONREVEAL_SOURCES})
target_include_directories(RegionReveal PRIVATE src)

if(REGIONREVEAL_CRASHDUMP)
  target_compile_definitions(dinput8 PRIVATE REGIONREVEAL_CRASHDUMP)
  target_compile_definitions(RegionReveal PRIVATE REGIONREVEAL_CRASHDUMP)
endif()
set_target_properties(RegionReveal PROPERTIES PREFIX "" OUTPUT_NAME "RegionReveal")

add_executable(RegionRevealLauncher tools/launcher/launcher.cpp)

# Resolves the shipped signatures against a Cube.exe on disk - the only part of
# the mod that can be checked without running the game.
#   python tests/run_tests.py build/Release/signature_test.exe "<path>/Cube.exe"
add_executable(signature_test tests/signature_test.cpp src/game/signatures.cpp)
target_include_directories(signature_test PRIVATE src)

# The visited-area file and the preview tile synthesis; needs neither the game
# nor a Cube.exe on disk.
add_executable(region_test tests/region_test.cpp src/region_reveal/visited.cpp
                           src/region_reveal/preview_tile.cpp src/region_reveal/log.cpp)
target_include_directories(region_test PRIVATE src)

foreach(target dinput8 RegionReveal RegionRevealLauncher signature_test region_test)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  endif()
  # windows.h would otherwise define min and max as macros over std::min/max.
  target_compile_definitions(${target} PRIVATE NOMINMAX)
endforeach()
