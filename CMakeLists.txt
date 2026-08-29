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

add_library(RegionReveal SHARED
  src/mod.cpp
  src/hooks.cpp
  src/game/signatures.cpp
  src/region_reveal/log.cpp
  src/region_reveal/reveal.cpp
)
target_include_directories(RegionReveal PRIVATE src)
set_target_properties(RegionReveal PROPERTIES PREFIX "" OUTPUT_NAME "RegionReveal")

add_executable(RegionRevealLauncher tools/launcher/launcher.cpp)

# Resolves the shipped signatures against a Cube.exe on disk - the only part of
# the mod that can be checked without running the game.
#   python tests/run_tests.py build/Release/signature_test.exe "<path>/Cube.exe"
add_executable(signature_test tests/signature_test.cpp src/game/signatures.cpp)
target_include_directories(signature_test PRIVATE src)

foreach(target RegionReveal RegionRevealLauncher signature_test)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  endif()
endforeach()
