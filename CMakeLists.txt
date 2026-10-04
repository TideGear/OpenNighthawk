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

# ---- The emulated PC: DOS, BIOS and devices ------------------------------
add_library(f117machine STATIC
  src/machine/machine.c src/machine/pc.c src/machine/dos.c)
target_include_directories(f117machine PUBLIC src/machine src/recomp)
target_link_libraries(f117machine PUBLIC f117cpu)

# ---- Presentation shared by the window and the headless runner -----------
add_library(f117present STATIC src/host/present.c src/host/keys.c)
target_include_directories(f117present PUBLIC src/host)
target_link_libraries(f117present PUBLIC f117machine)

# ---- The recomp runtime: generated code if there is any ------------------
add_library(f117recomp_none STATIC src/recomp/recomp.c src/recomp/recomp_empty.c)
target_link_libraries(f117recomp_none PUBLIC f117machine)

if(F117R_GEN_DIR)
  file(GLOB F117R_GEN_SOURCES CONFIGURE_DEPENDS "${F117R_GEN_DIR}/gen_*.c")
  add_library(f117recomp STATIC src/recomp/recomp.c ${F117R_GEN_SOURCES})
  target_link_libraries(f117recomp PUBLIC f117machine)
  if(MSVC)
    # Generated code: no warnings, and optimise each region on its own.
    set_source_files_properties(${F117R_GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0;/bigobj")
  endif()
endif()

# Headless runner: tests, captures, coverage, lockstep.
add_executable(f117run src/host/headless.c)
if(F117R_GEN_DIR)
  target_link_libraries(f117run PRIVATE f117present f117recomp)
else()
  target_link_libraries(f117run PRIVATE f117present f117recomp_none)
endif()

# ---- The game in a window ------------------------------------------------
if(F117R_BUILD_APP)
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    include(FetchContent)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG        release-3.2.10
      GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(SDL3)
  endif()

  # Nuked OPL3 1.8 (LGPL-2.1), the AdLib's chip.
  add_library(nukedopl3 STATIC third_party/nuked-opl3/opl3.c)
  target_include_directories(nukedopl3 PUBLIC third_party/nuked-opl3)
  if(MSVC)
    target_compile_options(nukedopl3 PRIVATE /W0)
  endif()

  set(F117R_APP_SOURCES src/host/main.c src/host/audio.c src/host/midistream.c)
  add_executable(f117a WIN32 ${F117R_APP_SOURCES})
  target_link_libraries(f117a PRIVATE f117present nukedopl3 SDL3::SDL3)
  if(F117R_GEN_DIR)
    target_link_libraries(f117a PRIVATE f117recomp)
  else()
    target_link_libraries(f117a PRIVATE f117recomp_none)
  endif()
  if(WIN32)
    target_link_libraries(f117a PRIVATE winmm)
    add_custom_command(TARGET f117a POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:f117a>)
  endif()
endif()

if(F117R_BUILD_TESTS)
  # Flat API over the core for the Python vector harnesses and the
  # unpacker. Self-contained: it supplies the two machine hooks itself.
  add_library(f117cpu_api SHARED tests/cpu_api.c src/cpu/cpu.c)
  target_include_directories(f117cpu_api PRIVATE src/cpu)
endif()
