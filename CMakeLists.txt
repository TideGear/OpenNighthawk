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
option(F117R_WITH_MT32EMU "Render Roland MIDI through Munt (the MT-32 ROMs are supplied at run time)" ON)
set(F117R_MT32EMU_SOURCE "" CACHE PATH "Optional Munt mt32emu source directory (default: fetched at a pinned commit)")
option(F117R_MT32EMU_SYSTEM "Use an installed libmt32emu CMake package instead of fetching Munt" OFF)
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
add_library(f117cpu STATIC src/cpu/cpu.c src/cpu/timing386.c)
target_include_directories(f117cpu PUBLIC src/cpu)

# ---- The emulated PC: DOS, BIOS and devices ------------------------------
add_library(f117machine STATIC
  src/machine/machine.c src/machine/pc.c src/machine/dos.c
  src/machine/dos_memory.c src/machine/dos_programs.c src/machine/dos_files.c
  src/machine/dos_keyboard.c src/machine/dos_video.c src/machine/mouse.c)
target_include_directories(f117machine PUBLIC src/machine src/recomp)
target_link_libraries(f117machine PUBLIC f117cpu)

# ---- Presentation shared by the window and the headless runner -----------
add_library(f117present STATIC src/host/present.c src/host/keys.c src/host/inputlog.c src/present/drawlist.c)
target_include_directories(f117present PUBLIC src/host src/present)
target_link_libraries(f117present PUBLIC f117machine)

# ---- The recomp runtime: generated code if there is any ------------------
# Switchable fixes attach to the runtime as code overrides (src/fixes).
add_library(f117recomp_none STATIC src/recomp/recomp.c src/recomp/recomp_empty.c src/fixes/fixes.c src/matched/matched.c src/matched/observe.c)
target_include_directories(f117recomp_none PUBLIC src/fixes src/matched)
target_link_libraries(f117recomp_none PUBLIC f117machine)

if(F117R_GEN_DIR)
  file(GLOB F117R_GEN_SOURCES CONFIGURE_DEPENDS "${F117R_GEN_DIR}/gen_*.c")
  add_library(f117recomp STATIC src/recomp/recomp.c src/fixes/fixes.c src/matched/matched.c src/matched/observe.c ${F117R_GEN_SOURCES})
  target_include_directories(f117recomp PUBLIC src/fixes src/matched)
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
  enable_language(CXX)
  if(MSVC)
    foreach(cfg CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELWITHDEBINFO)
      string(REPLACE "/Zi" "/Z7" ${cfg} "${${cfg}}")
    endforeach()
  endif()
  set(CMAKE_CXX_STANDARD 11)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  add_library(dbopl STATIC third_party/dosbox-dbopl/dbopl.cpp src/host/dbopl_bridge.cpp)
  target_include_directories(dbopl PRIVATE third_party/dosbox-dbopl PUBLIC src/host)
  if(MSVC)
    set_source_files_properties(third_party/dosbox-dbopl/dbopl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
  endif()
  add_executable(dbopl_render tools/dbopl_render.c)
  target_link_libraries(dbopl_render PRIVATE dbopl)
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

  add_library(f117mt32 STATIC src/host/mt32.c src/host/midistream.c)
  target_include_directories(f117mt32 PUBLIC src/host)
  if(F117R_WITH_MT32EMU)
    if(F117R_MT32EMU_SYSTEM)
      find_package(MT32Emu 2.7 CONFIG REQUIRED)
    else()
      # Munt's mt32emu (LGPL-2.1-or-later) as a shared library beside the
      # executables: a given checkout, or the pinned commit (2.8.3 era).
      enable_language(CXX)
      set(libmt32emu_SHARED ON CACHE BOOL "" FORCE)
      set(libmt32emu_C_INTERFACE ON CACHE BOOL "" FORCE)
      set(libmt32emu_BUILD_TESTING OFF CACHE BOOL "" FORCE)
      if(F117R_MT32EMU_SOURCE)
        add_subdirectory("${F117R_MT32EMU_SOURCE}" mt32emu EXCLUDE_FROM_ALL)
      else()
        include(FetchContent)
        FetchContent_Declare(munt
          GIT_REPOSITORY https://github.com/munt/munt.git
          GIT_TAG        6e7c01fba7e1d50c8fa705834889fd0eac136075
          SOURCE_SUBDIR  mt32emu)
        FetchContent_MakeAvailable(munt)
      endif()
    endif()
    target_compile_definitions(f117mt32 PRIVATE F117R_HAS_MT32EMU)
    target_link_libraries(f117mt32 PUBLIC MT32Emu::mt32emu)
  endif()

  function(f117r_audio_target name)
    target_link_libraries(${name} PRIVATE f117present nukedopl3 dbopl f117mt32)
    if(WIN32 AND F117R_WITH_MT32EMU)
      add_custom_command(TARGET ${name} POST_BUILD COMMAND ${CMAKE_COMMAND} -E
        copy_if_different $<TARGET_FILE:MT32Emu::mt32emu> $<TARGET_FILE_DIR:${name}>)
    endif()
  endfunction()

  add_executable(audio_render tools/audio_render.c src/host/audio.c src/host/speaker.c)
  f117r_audio_target(audio_render)

  set(F117R_APP_SOURCES src/host/main.c src/host/audio.c src/host/speaker.c src/host/config.c)
  add_executable(f117a WIN32 ${F117R_APP_SOURCES})
  f117r_audio_target(f117a)
  target_link_libraries(f117a PRIVATE SDL3::SDL3)
  # the commented settings file, beside the executable to copy as f117a.ini
  add_custom_command(TARGET f117a POST_BUILD COMMAND ${CMAKE_COMMAND} -E
    copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/f117a.example.ini $<TARGET_FILE_DIR:f117a>)
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
  add_library(f117cpu_api SHARED tests/cpu_api.c src/cpu/cpu.c src/cpu/timing386.c)
  target_include_directories(f117cpu_api PRIVATE src/cpu)

  # Read-only observation plus normal input for diagnostic pilots.
  add_library(f117machine_api SHARED src/host/machine_api.c)
  target_link_libraries(f117machine_api PRIVATE f117present)
  if(F117R_GEN_DIR)
    target_link_libraries(f117machine_api PRIVATE f117recomp)
  else()
    target_link_libraries(f117machine_api PRIVATE f117recomp_none)
  endif()
  if(NOT WIN32)
    set_target_properties(f117cpu f117machine f117present f117recomp_none
      PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(F117R_GEN_DIR)
      set_target_properties(f117recomp PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
  endif()

  # Hand tests of the 80186/80286 instructions the flight engine uses, and
  # of the corners where those parts differ from the 8086 (the Reimp's).
  enable_testing()
  add_executable(test_host_clock tests/test_host_clock.c)
  target_include_directories(test_host_clock PRIVATE src/host)
  add_test(NAME host_wall_clock COMMAND test_host_clock)
  if(F117R_BUILD_APP)
    add_executable(test_audio tests/test_audio.c src/host/audio.c src/host/speaker.c)
    f117r_audio_target(test_audio)
    add_test(NAME host_timestamped_audio COMMAND test_audio)
    add_executable(test_midistream tests/test_midistream.c src/host/midistream.c)
    target_include_directories(test_midistream PRIVATE src/host)
    add_test(NAME host_midi_stream COMMAND test_midistream)
    add_executable(test_dbopl tests/test_dbopl.c)
    target_link_libraries(test_dbopl PRIVATE dbopl)
    add_test(NAME dbopl_render_chunks COMMAND test_dbopl)
    add_executable(test_host_keys tests/test_host_keys.c)
    target_include_directories(test_host_keys PRIVATE src/host)
    target_link_libraries(test_host_keys PRIVATE SDL3::SDL3)
    add_test(NAME host_keyboard_mapping COMMAND test_host_keys)
    add_executable(test_config tests/test_config.c src/host/config.c)
    target_include_directories(test_config PRIVATE src/host)
    add_test(NAME host_config_file COMMAND test_config)
  endif()
  add_executable(test186 tests/test186.c tests/cpu_stubs.c)
  target_link_libraries(test186 PRIVATE f117cpu)
  add_test(NAME cpu_186_286 COMMAND test186)

  add_executable(test_mouse tests/test_mouse.c)
  target_link_libraries(test_mouse PRIVATE f117present f117recomp_none)
  add_test(NAME software_mouse_cursor COMMAND test_mouse)

  add_executable(test_video_ports tests/test_video_ports.c)
  target_link_libraries(test_video_ports PRIVATE f117recomp_none)
  add_test(NAME vga_dac_ports COMMAND test_video_ports)

  add_executable(test_scanout tests/test_scanout.c)
  target_link_libraries(test_scanout PRIVATE f117present f117recomp_none)
  add_test(NAME vga_scanout COMMAND test_scanout)

  # The Stage 1 replay against an observer log's page dumps (a log is needed:
  # test_drawlist LOG, see docs/presentation.md).
  add_executable(test_drawlist tests/test_drawlist.c)
  target_link_libraries(test_drawlist PRIVATE f117present)

  add_executable(test_overrides tests/test_overrides.c)
  target_link_libraries(test_overrides PRIVATE f117present f117recomp_none)
  add_test(NAME code_overrides COMMAND test_overrides)

  # Every translated instruction of the game against the interpreter, one
  # instruction at a time from random states (needs the generated code).
  if(F117R_GEN_DIR)
    # Only the generated objects and the core are pulled from the static
    # libraries: the test supplies the machine hooks itself.
    add_executable(insn_lockstep tests/insn_lockstep.c)
    target_link_libraries(insn_lockstep PRIVATE f117recomp)
    add_test(NAME insn_lockstep COMMAND insn_lockstep --states 4)
    # Phase 2: every matched routine against the original from random states.
    add_executable(func_lockstep tests/func_lockstep.c)
    target_link_libraries(func_lockstep PRIVATE f117recomp)
    add_test(NAME func_lockstep COMMAND func_lockstep --states 2000)
  endif()
endif()
