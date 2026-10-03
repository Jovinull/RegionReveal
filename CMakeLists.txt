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

# The mod, built as dinput8.dll: Cube.exe imports one function from it and
# Windows looks in the game's own folder first, so dropping it beside Cube.exe
# loads the mod with no injector. Nothing is renamed or replaced.
add_library(dinput8 SHARED
  src/mod.cpp
  src/hooks.cpp
  src/loader/proxy_dinput8.cpp
  src/game/session.cpp
  src/game/signatures.cpp
  src/game/world.cpp
  src/region_reveal/areas.cpp
  src/region_reveal/log.cpp
  src/region_reveal/paths.cpp
  src/region_reveal/reveal.cpp
  src/region_reveal/visited.cpp
)
target_include_directories(dinput8 PRIVATE src)
set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")

# Resolves the shipped signatures against a Cube.exe on disk.
#   python tests/run_tests.py build/Release/signature_test.exe "<path>/Cube.exe"
add_executable(signature_test tests/signature_test.cpp src/game/signatures.cpp)
target_include_directories(signature_test PRIVATE src)

# The logic that needs neither the game nor a Cube.exe: the visited-area file,
# the revealed-area set, the per-cell answers and the area-lookup geometry.
add_executable(region_test
  tests/region_test.cpp
  src/game/world.cpp
  src/game/signatures.cpp
  src/region_reveal/areas.cpp
  src/region_reveal/log.cpp
  src/region_reveal/paths.cpp
  src/region_reveal/visited.cpp
)
target_include_directories(region_test PRIVATE src)

foreach(target dinput8 signature_test region_test)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  endif()
  # windows.h would otherwise define min and max as macros over std::min/max.
  target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endforeach()
