cmake_minimum_required(VERSION 3.24)
project(f117recomp C)

# F-117A Recomp: the original DOS game's machine code translated to C and
# run on an emulated PC. See README.md and docs/architecture.md.
#
# The translated code is generated at build time from the user's own copy
# of the game (F117R_GEN_DIR, written by tools/recomp.py) and is never part
# of this repository.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(F117R_BUILD_APP   "Build the SDL3 game executable" ON)
option(F117R_BUILD_TESTS "Build the CPU test library and tools" ON)
set(F117R_GEN_DIR "" CACHE PATH
    "Directory holding the generated translation (recomp_gen.c etc.); empty builds an interpreter-only executable")

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  # The working copy lives in a synced folder: PDBs are written by many
  # compiler processes at once and the sync client locks them. Embedded
  # debug info avoids the shared PDB entirely.
  foreach(cfg CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELWITHDEBINFO)
    string(REPLACE "/Zi" "/Z7" ${cfg} "${${cfg}}")
  endforeach()
  string(REPLACE "/Ob1" "/Ob2" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---- CPU core: the reference interpreter and the shared semantics -------
add_library(f117cpu STATIC src/cpu/cpu.c)
target_include_directories(f117cpu PUBLIC src/cpu)

if(F117R_BUILD_TESTS)
  # Flat API over the core for the Python vector harnesses and the
  # unpacker. Self-contained: it supplies the two machine hooks itself.
  add_library(f117cpu_api SHARED tests/cpu_api.c src/cpu/cpu.c)
  target_include_directories(f117cpu_api PRIVATE src/cpu)
endif()
