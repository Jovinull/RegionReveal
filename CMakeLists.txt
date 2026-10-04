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
  src/threads.cpp
  src/game/label_passes.cpp
  src/game/label_text.cpp
  src/game/session.cpp
  src/game/signatures.cpp
  src/game/world.cpp
  src/region_reveal/areas.cpp
  src/region_reveal/log.cpp
  src/region_reveal/marks.cpp
  src/region_reveal/paths.cpp
  src/region_reveal/reveal.cpp
  src/region_reveal/settings.cpp
  src/region_reveal/visited.cpp
)

# The mod, built as dinput8.dll: Cube.exe imports one function from it and
# Windows looks in the game's own folder first, so dropping it beside Cube.exe
# loads the mod with no injector. Nothing is renamed or replaced.
add_library(dinput8 SHARED ${REGIONREVEAL_SOURCES} src/loader/proxy_dinput8.cpp)
target_include_directories(dinput8 PRIVATE src)
set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")

# The same mod for a mod loader - the Cube World Mod Launcher's Mods folder or
# any injector - for players whose dinput8.dll is already taken by ReShade or
# ENB.
add_library(RegionReveal SHARED ${REGIONREVEAL_SOURCES})
target_include_directories(RegionReveal PRIVATE src)
set_target_properties(RegionReveal PROPERTIES PREFIX "" OUTPUT_NAME "RegionReveal")

# Resolves the shipped signatures against a Cube.exe on disk.
#   python tests/run_tests.py build/Release/signature_test.exe "<path>/Cube.exe"
add_executable(signature_test tests/signature_test.cpp src/game/signatures.cpp src/game/label_passes.cpp
                              src/game/label_text.cpp)
target_include_directories(signature_test PRIVATE src)

# The logic that needs neither the game nor a Cube.exe: the visited-area file,
# the revealed-area set, the per-cell answers and the area-lookup geometry.
add_executable(region_test
  tests/region_test.cpp
  src/game/world.cpp
  src/game/signatures.cpp
  src/region_reveal/areas.cpp
  src/region_reveal/log.cpp
  src/region_reveal/marks.cpp
  src/region_reveal/paths.cpp
  src/region_reveal/visited.cpp
)

# Loads dinput8.dll into a process that is not Cube.exe: the mod must find
# nothing to hook, say so in the log, and still forward DirectInput8Create.
add_executable(host_test tests/host_test.cpp src/threads.cpp)
target_include_directories(host_test PRIVATE src)
target_include_directories(region_test PRIVATE src)

foreach(target dinput8 RegionReveal signature_test region_test host_test)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
  endif()
  # windows.h would otherwise define min and max as macros over std::min/max.
  target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endforeach()
