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
add_library(f117present STATIC src/host/present.c src/host/keys.c src/host/inputlog.c
  src/present/drawlist.c src/present/drawfeed.c src/present/interp.c src/present/hires.c src/present/nearclip.c src/present/hud_state.c src/present/hud_cover.c src/present/hud_overlay.c src/present/band_state.c src/present/scene_mesh.c src/present/rerun.c)
target_include_directories(f117present PUBLIC src/host src/present src/matched)
target_link_libraries(f117present PUBLIC f117machine)

# ---- The recomp runtime: generated code if there is any ------------------
# Switchable fixes attach to the runtime as code overrides (src/fixes).
# Both runtimes use the same matched bodies and compile settings. Compile
# this large source once, then include its object in each runtime archive.
add_library(f117matched OBJECT src/matched/matched.c)
target_include_directories(f117matched PRIVATE src/fixes src/matched)
if(MSVC)
  # /Ot (favour speed, part of /O2) sends MSVC's optimiser into 90+ minute compiles of this file since batch ab (the
  # big VGAME routines reach 10,000 to 125,000 instructions after inlining); /Os compiles it in about 6 minutes with
  # the same semantics. Measured 20 Oct 2026: /O2 /Ob2 (still running at 90 min), /O2 /Os (6.5 min), /O1 (4.5 min),
  # /O2 /Ob0 (10 s, but the forced-inline CPU helpers become calls).
  set_source_files_properties(src/matched/matched.c PROPERTIES COMPILE_OPTIONS /Os)
endif()
target_link_libraries(f117matched PRIVATE f117machine)
add_library(f117recomp_none STATIC src/recomp/recomp.c src/recomp/recomp_empty.c src/fixes/fixes.c src/matched/observe.c $<TARGET_OBJECTS:f117matched>)
target_include_directories(f117recomp_none PUBLIC src/fixes src/matched)
target_link_libraries(f117recomp_none PUBLIC f117machine)

if(F117R_GEN_DIR)
  file(GLOB F117R_GEN_SOURCES CONFIGURE_DEPENDS "${F117R_GEN_DIR}/gen_*.c")
  add_library(f117recomp STATIC src/recomp/recomp.c src/fixes/fixes.c src/matched/observe.c $<TARGET_OBJECTS:f117matched> ${F117R_GEN_SOURCES})
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

  set(F117R_APP_SOURCES src/host/main.c src/host/audio.c src/host/speaker.c src/host/config.c src/host/settings.c src/host/crt.c src/host/controls.c src/host/remap.c)
  add_executable(f117a WIN32 ${F117R_APP_SOURCES})
  f117r_audio_target(f117a)
  target_link_libraries(f117a PRIVATE SDL3::SDL3)
  # the commented settings file, beside the executable to copy as f117a.ini
  add_custom_command(TARGET f117a POST_BUILD COMMAND ${CMAKE_COMMAND} -E
    copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/f117a.example.ini $<TARGET_FILE_DIR:f117a>)
  add_custom_command(TARGET f117a POST_BUILD COMMAND ${CMAKE_COMMAND} -E
    copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/f117a.4k.ini $<TARGET_FILE_DIR:f117a>)
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
    set_target_properties(f117cpu f117machine f117present f117matched f117recomp_none
      PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(F117R_GEN_DIR)
      set_target_properties(f117recomp PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
  endif()

  # Hand tests of the 80186/80286 instructions the flight engine uses, and
  # of the corners where those parts differ from the 8086 (the Reimp's).
  enable_testing()
  add_executable(test_present_guard tests/test_present_guard.c)
  target_link_libraries(test_present_guard PRIVATE f117present)
  add_test(NAME presentation_flat_guard COMMAND test_present_guard)
  add_executable(test_present_stream tests/test_present_stream.c)
  target_link_libraries(test_present_stream PRIVATE f117present f117recomp_none)
  add_test(NAME presentation_streaming COMMAND test_present_stream)
  add_executable(test_weapon_sweep tests/test_weapon_sweep.c)
  target_include_directories(test_weapon_sweep PRIVATE src/fixes)
  add_test(NAME weapon_proximity_sweep COMMAND test_weapon_sweep)
  add_executable(test_frame_pacer tests/test_frame_pacer.c)
  target_include_directories(test_frame_pacer PRIVATE src/fixes)
  add_test(NAME real_time_frame_pacer COMMAND test_frame_pacer)
  add_executable(test_inputlog tests/test_inputlog.c)
  target_link_libraries(test_inputlog PRIVATE f117present f117recomp_none)
  add_test(NAME recorded_simulation_cadence COMMAND test_inputlog)
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
    add_executable(test_settings tests/test_settings.c src/host/settings.c src/host/config.c)
    target_include_directories(test_settings PRIVATE src/host)
    add_test(NAME host_settings_menu COMMAND test_settings)
    add_executable(test_crt tests/test_crt.c src/host/crt.c)
    target_include_directories(test_crt PRIVATE src/host)
    add_test(NAME host_crt_filter COMMAND test_crt)
    add_executable(test_controls tests/test_controls.c src/host/controls.c src/host/config.c)
    target_include_directories(test_controls PRIVATE src/host)
    add_test(NAME host_controller_mapping COMMAND test_controls)
    add_executable(test_remap tests/test_remap.c src/host/remap.c)
    target_include_directories(test_remap PRIVATE src/host)
    target_link_libraries(test_remap PRIVATE SDL3::SDL3)
    add_test(NAME host_keyboard_remap COMMAND test_remap)
  endif()
  add_executable(test186 tests/test186.c tests/cpu_stubs.c)
  target_link_libraries(test186 PRIVATE f117cpu)
  add_test(NAME cpu_186_286 COMMAND test186)

  add_executable(test_reloc_image tests/test_reloc_image.c tests/test_reloc_fixture.c tests/cpu_stubs.c)
  target_include_directories(test_reloc_image PRIVATE src/recomp)
  target_link_libraries(test_reloc_image PRIVATE f117cpu)
  add_test(NAME relocated_image_loader COMMAND test_reloc_image)

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
  # Stage 3's in-between frames against the same log (test_interp LOG).
  add_executable(test_interp tests/test_interp.c)
  target_link_libraries(test_interp PRIVATE f117present)
  # Stage 2's sub-pixel polygons against the same log (test_hires LOG N).
  add_executable(test_hires tests/test_hires.c)
  target_link_libraries(test_hires PRIVATE f117present)
  add_executable(test_present_lines tests/test_present_lines.c)
  target_link_libraries(test_present_lines PRIVATE f117present)
  add_test(NAME presentation_hud_lines COMMAND test_present_lines)
  add_executable(test_world_lines tests/test_world_lines.c)
  target_link_libraries(test_world_lines PRIVATE f117present)
  add_test(NAME presentation_world_lines COMMAND test_world_lines)
  add_executable(test_world_jitter tests/test_world_jitter.c)
  target_link_libraries(test_world_jitter PRIVATE f117present f117recomp_none)
  # The state-driven HUD against the original's draw log (a private corpus: D:/f117-gate/present-v2/hud/corpus).
  add_executable(test_hud_state tests/test_hud_state.c)
  target_link_libraries(test_hud_state PRIVATE f117present)
  add_test(NAME presentation_hud_state COMMAND test_hud_state D:/f117-gate/present-v2/hud/corpus)
  set_tests_properties(presentation_hud_state PROPERTIES SKIP_RETURN_CODE 77)
  # ... both passes and the map arrow against observer logs carrying the HUD's state records (private: present-v2/hud2).
  add_executable(test_hud_logs tests/test_hud_logs.c)
  target_link_libraries(test_hud_logs PRIVATE f117present)
  add_test(NAME presentation_hud_logs COMMAND test_hud_logs D:/f117-gate/present-v2/hud2/corpus/list.txt)
  set_tests_properties(presentation_hud_logs PROPERTIES SKIP_RETURN_CODE 77)
  add_executable(test_map_marker tests/test_map_marker.c)
  target_link_libraries(test_map_marker PRIVATE f117present)
  add_test(NAME presentation_map_marker COMMAND test_map_marker)
  add_executable(test_hud_observer tests/test_hud_observer.c)
  target_link_libraries(test_hud_observer PRIVATE f117present f117recomp_none)
  add_test(NAME presentation_hud_observer COMMAND test_hud_observer)
  add_executable(test_observer_rows tests/test_observer_rows.c src/matched/observe.c)
  target_link_libraries(test_observer_rows PRIVATE f117present)
  add_test(NAME presentation_observer_rows COMMAND test_observer_rows)
  add_executable(test_scene_observer tests/test_scene_observer.c)
  target_link_libraries(test_scene_observer PRIVATE f117present f117recomp_none)
  add_test(NAME presentation_scene_observer COMMAND test_scene_observer)
  add_executable(test_scene_mesh tests/test_scene_mesh.c)
  target_link_libraries(test_scene_mesh PRIVATE f117present)
  add_test(NAME presentation_scene_mesh COMMAND test_scene_mesh)
  # ... and through the live path (a strike window recorded with F117R_OBSERVE_PAGES=1 F117R_OBSERVE_HUD=1).
  add_executable(test_hud_live tests/test_hud_live.c)
  target_link_libraries(test_hud_live PRIVATE f117present f117recomp_none)
  add_test(NAME presentation_hud_live COMMAND test_hud_live D:/f117-gate/present-v2/hud/w_strike853/w.log 4 --leak-check)
  # How smoothly the HUD moves between logic frames, before and after (a measurement, not a pass/fail test).
  add_executable(test_hud_jitter tests/test_hud_jitter.c)
  target_link_libraries(test_hud_jitter PRIVATE f117present f117recomp_none)
  set_tests_properties(presentation_hud_live PROPERTIES SKIP_RETURN_CODE 77)
  # The state-driven sky and ground bands against the original's draw log (private corpus: D:/f117-gate/present-v2/horizon/corpus).
  add_executable(test_band_rules tests/test_band_rules.c)
  target_link_libraries(test_band_rules PRIVATE f117present)
  add_test(NAME presentation_band_rules COMMAND test_band_rules)
  add_executable(test_band_observer tests/test_band_observer.c)
  target_link_libraries(test_band_observer PRIVATE f117present f117recomp_none)
  add_test(NAME presentation_band_observer COMMAND test_band_observer)
  add_executable(test_rerun tests/test_rerun.c)
  target_link_libraries(test_rerun PRIVATE f117present f117recomp_none)
  add_test(NAME presentation_rerun_isolation COMMAND test_rerun)
  add_executable(test_drawlive_clock tests/test_drawlive_clock.c)
  target_link_libraries(test_drawlive_clock PRIVATE f117present f117recomp_none)
  add_test(NAME presentation_playback_clock COMMAND test_drawlive_clock)
  add_executable(test_band_state tests/test_band_state.c)
  target_link_libraries(test_band_state PRIVATE f117present)
  add_test(NAME presentation_band_state COMMAND test_band_state D:/f117-gate/present-v2/horizon/corpus)
  set_tests_properties(presentation_band_state PROPERTIES SKIP_RETURN_CODE 77)
  # ... and through the live path (a strike window recorded with F117R_OBSERVE_PAGES=1 F117R_OBSERVE_HUD=1 F117R_OBSERVE_BANDS=1).
  add_executable(test_band_live tests/test_band_live.c)
  target_link_libraries(test_band_live PRIVATE f117present f117recomp_none)
  add_test(NAME presentation_band_live COMMAND test_band_live D:/f117-gate/present-v2/horizon/w_strike/w.log 4)
  set_tests_properties(presentation_band_live PROPERTIES SKIP_RETURN_CODE 77)
  # How smoothly the horizon moves between logic frames, before and after (a measurement, not a pass/fail test).
  add_executable(test_band_jitter tests/test_band_jitter.c)
  target_link_libraries(test_band_jitter PRIVATE f117present f117recomp_none)
  add_executable(test_present_strips tests/test_present_strips.c)
  target_link_libraries(test_present_strips PRIVATE f117present)
  add_test(NAME presentation_span_polygons COMMAND test_present_strips)

  add_executable(test_present_map tests/test_present_map.c)
  target_link_libraries(test_present_map PRIVATE f117present)
  add_test(NAME presentation_map COMMAND test_present_map)
  add_executable(test_nearclip tests/test_nearclip.c)
  target_link_libraries(test_nearclip PRIVATE f117present)
  add_test(NAME presentation_near_crossing COMMAND test_nearclip)
  add_executable(test_near_polygons tests/test_near_polygons.c)
  target_link_libraries(test_near_polygons PRIVATE f117present)
  add_test(NAME presentation_near_polygons COMMAND test_near_polygons)

  add_executable(test_overrides tests/test_overrides.c)
  target_link_libraries(test_overrides PRIVATE f117present f117recomp_none)
  add_test(NAME code_overrides COMMAND test_overrides)

  add_executable(test_native_boundary tests/test_native_boundary.c)
  target_link_libraries(test_native_boundary PRIVATE f117present f117recomp_none)
  add_test(NAME native_service_boundary COMMAND test_native_boundary)
  add_executable(test_stale_override tests/test_stale_override.c)
  target_link_libraries(test_stale_override PRIVATE f117present f117recomp_none)
  add_test(NAME stale_override COMMAND test_stale_override)

  # Every translated instruction of the game against the interpreter, one
  # instruction at a time from random states (needs the generated code).
  if(F117R_GEN_DIR)
    # Only the generated objects and the core are pulled from the static
    # libraries: the test supplies the machine hooks itself.
    add_executable(insn_lockstep tests/insn_lockstep.c)
    add_executable(test_present_fills tests/test_present_fills.c)
    target_link_libraries(test_present_fills PRIVATE f117present f117recomp)
    add_test(NAME presentation_fill_styles COMMAND test_present_fills)
    add_executable(test_present_driver tests/test_present_driver.c)
    target_link_libraries(test_present_driver PRIVATE f117present f117recomp)
    add_test(NAME presentation_driver_branches COMMAND test_present_driver)
    target_link_libraries(insn_lockstep PRIVATE f117recomp)
    add_test(NAME insn_lockstep COMMAND insn_lockstep --states 4)
    # Phase 2: every matched routine against the original from random states.
    add_executable(check_q7 tests/check_q7.c)
    target_link_libraries(check_q7 PRIVATE f117present f117recomp)
    add_executable(check_drivers tests/check_drivers.c)
    target_link_libraries(check_drivers PRIVATE f117present f117recomp)
    add_executable(check_q17 tests/check_q17.c)
    target_link_libraries(check_q17 PRIVATE f117present f117recomp)
    add_executable(check_q18 tests/check_q18.c)
    target_link_libraries(check_q18 PRIVATE f117present f117recomp)
    add_executable(check_q19 tests/check_q19.c)
    target_link_libraries(check_q19 PRIVATE f117present f117recomp)
    add_executable(check_q20 tests/check_q20.c)
    target_link_libraries(check_q20 PRIVATE f117present f117recomp)
    add_executable(check_q13a tests/check_q13a.c)
    target_link_libraries(check_q13a PRIVATE f117present f117recomp)
    add_executable(check_q21 tests/check_q21.c)
    target_link_libraries(check_q21 PRIVATE f117present f117recomp)
    add_executable(check_q22 tests/check_q22.c)
    target_link_libraries(check_q22 PRIVATE f117present f117recomp)
    add_executable(check_q4 tests/check_q4.c)
    target_link_libraries(check_q4 PRIVATE f117present f117recomp)
    add_executable(check_q13b tests/check_q13b.c)
    target_link_libraries(check_q13b PRIVATE f117present f117recomp)
    add_executable(check_d18 tests/check_d18.c)
    target_link_libraries(check_d18 PRIVATE f117present f117recomp)
    add_executable(check_d101 tests/check_d101.c)
    target_link_libraries(check_d101 PRIVATE f117present f117recomp)
    add_executable(check_q12b tests/check_q12b.c)
    target_link_libraries(check_q12b PRIVATE f117present f117recomp)
    add_executable(check_q23 tests/check_q23.c)
    target_link_libraries(check_q23 PRIVATE f117present f117recomp)
    add_executable(check_q14 tests/check_q14.c)
    target_link_libraries(check_q14 PRIVATE f117present f117recomp)
    add_executable(check_q12 tests/check_q12.c)
    target_link_libraries(check_q12 PRIVATE f117present f117recomp)
    add_executable(check_q11 tests/check_q11.c)
    target_link_libraries(check_q11 PRIVATE f117present f117recomp)
    add_executable(func_lockstep tests/func_lockstep.c)
    target_link_libraries(func_lockstep PRIVATE f117recomp)
    add_test(NAME func_lockstep COMMAND func_lockstep --states 2000)
    add_test(NAME matched_module_rules COMMAND func_lockstep --check-module-rules)
    # A body that charges the clock in one lump around an OUT, one that runs on past an EOI write, one
    # whose EOI comes past the run's limit, and which stops complete their named region.
    add_test(NAME matched_io_rules COMMAND func_lockstep --check-io-rules)
    # Eight bodies that ran on past their own counter 0 control word to port 43h (a wake); this seed's
    # first 100 states showed all eight before they stopped after that OUT (21 Oct).
    add_test(NAME matched_wake_stops COMMAND func_lockstep --states 100 --seed 0x5EED0F117A
      --only PLAYER.EXE:244,VGAME.EXE:235,START.EXE:8DA4,END.EXE:4C88,VGAME.EXE:1D54,VGAME.EXE:1D01,START.EXE:8D71,END.EXE:4C55)
    add_test(NAME matched_initializer_bounds COMMAND func_lockstep --states 4000
      --seed 0x66A860898896 --only SETUP.EXE:177C)
    # This seed aliases the shadow-text source with PUSH SS's stack write.
    add_test(NAME matched_shadow_stack_alias COMMAND func_lockstep --states 4000
      --seed 0xf2a7d2c1bf36 --only START.EXE:36C4,END.EXE:19C3,END.EXE:112A)
    # The random generator's seed aliases the caller's bound through DS and SS.
    add_test(NAME matched_start_rnd_stack_alias COMMAND func_lockstep --states 4000
      --seed 0x4ebc42e1d00b --only START.EXE:76C0)
    # PUSH BP inserts a zero in strupr's input through overlapping DS/SS.
    add_test(NAME matched_strupr_stack_alias COMMAND func_lockstep --states 4000
      --seed 0xfe825bd272ab --only VGAME.EXE:EDC2)
    # The scene origin reads each argument just before its store; DS and SS can overlap (seed of 7c on 20 Oct).
    add_test(NAME matched_scene_origin_stack_alias COMMAND func_lockstep --states 4000
      --seed 0x8b9b2367ecdf --only VGAME.EXE:D9E7)
    # A stack overlapping the shift helper changes BX before the in-place stores.
    add_test(NAME matched_shift_stack_alias COMMAND func_lockstep --states 4000
      --seed 0x2cb230b9369f --only VGAME.EXE:EF80,START.EXE:97D8)
    # A random pair swap temporarily overwrites and then restores its own epilogue.
    add_test(NAME matched_order_code_alias COMMAND func_lockstep --states 4000
      --seed 0xc5ce5129a4d9 --only START.EXE:39B3,START.EXE:39D5,END.EXE:1CB1,END.EXE:1CD3)
    add_test(NAME matched_pause_wait COMMAND func_lockstep --states 4000
      --only VGAME.EXE:3A4E)
    # Aliased data temporarily changes a format-reader RET and lower clip code.
    add_test(NAME matched_frame_code_alias COMMAND func_lockstep --states 4000
      --seed 0x5361129204e7 --only PLAYER.EXE:1E90,VGAME.EXE:577)
    add_test(NAME matched_hud_layouts COMMAND func_lockstep --states 4000
      --only VGAME.EXE:9D8,VGAME.EXE:A2C)
    add_test(NAME matched_camera_darken COMMAND func_lockstep --states 4000
      --only VGAME.EXE:C45C)
    # A store through px can land on the stack slot still holding y or py.
    add_test(NAME matched_map_project_stack_alias COMMAND func_lockstep --states 4000
      --seed 0x2fa6695118ca --only VGAME.EXE:13B9)
    # The saved BP can wrap onto the scale word it divides by (SS:SP-2 past 1 MB onto DS:950E).
    add_test(NAME matched_map_project_saved_bp_alias COMMAND func_lockstep --states 4000
      --seed 0xbf2b70af59c7 --only VGAME.EXE:13B9)
    add_test(NAME matched_control_break COMMAND func_lockstep --states 4000
      --only VGAME.EXE:1B73,START.EXE:85B8,END.EXE:4557,DSWAP.EXE:523,SETUP.EXE:C35)
    add_test(NAME matched_dswap_memory COMMAND func_lockstep --states 4000
      --only DSWAP.EXE:4E0,DSWAP.EXE:506)
    add_test(NAME matched_control_break_restore COMMAND func_lockstep --states 4000
      --only VGAME.EXE:1B96,START.EXE:85DB,END.EXE:457A,DSWAP.EXE:546,SETUP.EXE:C58)
    add_test(NAME matched_dswap_allocation_error COMMAND func_lockstep --states 4000
      --only DSWAP.EXE:494)
    add_test(NAME matched_projected_diamond COMMAND func_lockstep --states 4000
      --only VGAME.EXE:B0C2)
    add_test(NAME matched_screen_loader COMMAND func_lockstep --states 4000
      --only START.EXE:3641)
    add_test(NAME matched_camera_gradients COMMAND func_lockstep --states 4000
      --only VGAME.EXE:1974,VGAME.EXE:19FD)
    add_test(NAME matched_sprite_map COMMAND func_lockstep --states 4000
      --only MPS_LOGO.EXE:678,VGAME.EXE:11A8)
    # The target-slot walk must count its loop and join instructions.
    add_test(NAME matched_target_damage_loop COMMAND func_lockstep --states 4000
      --seed 0x049040951e8d --only VGAME.EXE:73C8)
    # `or ax, ax` after a returning call is a word test: at this seed a byte-wide flag test gave the
    # wrong sign (BAA2); a zero low byte would also take the wrong branch (BAA2, 73C8's two calls).
    add_test(NAME matched_or_ax_word COMMAND func_lockstep --states 4000
      --seed 0xfaf385733f77 --only VGAME.EXE:BAA2,VGAME.EXE:73C8)
    # Divide-error handlers entered through a planted interrupt frame; half
    # the states save the faulting IDIV's own IP, which picks the denominator.
    add_test(NAME matched_divide_interrupts COMMAND func_lockstep --states 4000
      --only VGAME.EXE:84E)
    # Airfield worlds (carrier wire, field contact, objectives, tower); the
    # frame's sub-passes return at once.
    add_test(NAME matched_frame_update COMMAND func_lockstep --states 4000
      --only VGAME.EXE:3ADA)
    # Includes zero-divisor and overflowing reloads, checked at the original
    # divide-error frame after the named code hands back at IDIV.
    add_test(NAME matched_ground_defences COMMAND func_lockstep --states 4000
      --only VGAME.EXE:5046)
    # Formation, pursuit, patrol, landing and obstacle-probe traffic worlds.
    add_test(NAME matched_air_traffic COMMAND func_lockstep --states 4000
      --only VGAME.EXE:5852)
    # Mission generation: objectives, bases, route retries and divide faults.
    add_test(NAME matched_mission_generation COMMAND func_lockstep --states 4000
      --only START.EXE:4239)
    # Setup: questions, driver names, calibration and extended-key handling.
    add_test(NAME matched_setup_questions COMMAND func_lockstep --states 4000
      --only SETUP.EXE:10)
    # Logo sprite-set loader: streams, allocation/format failures and frame fixups.
    add_test(NAME matched_logo_sprite_set COMMAND func_lockstep --states 4000
      --only MPS_LOGO.EXE:112)
    # View compose: every view mode, cockpit/outside switches, tracked objects.
    add_test(NAME matched_view_compose COMMAND func_lockstep --states 4000
      --only VGAME.EXE:2F9D)
    # Incoming/player weapons, complete branch coverage and divide-error frames.
    add_test(NAME matched_projectiles COMMAND func_lockstep --states 4000
      --only VGAME.EXE:683E)
    # Random DS aliases destroy a callee RET; retain full-state comparison
    # without mistaking the original's fallthrough for a named-code escape.
    add_test(NAME matched_projectile_callee_code_alias COMMAND func_lockstep --states 4000
      --seed 0x5b0a262588dc --only VGAME.EXE:683E)
    # Planted keys, quit-prompt answers, ejection, touchdown and crash states;
    # an escaped callee return or thin coverage fails the test.
    add_test(NAME matched_flight_input COMMAND func_lockstep --states 4000
      --only VGAME.EXE:1EB0)
    add_test(NAME matched_camera_image COMMAND func_lockstep --states 4000
      --only VGAME.EXE:BCEF)
    add_test(NAME matched_moving_objects COMMAND func_lockstep --states 4000
      --only VGAME.EXE:983E)
    add_test(NAME matched_hud_pass COMMAND func_lockstep --states 4000
      --only VGAME.EXE:16)
    add_test(NAME matched_panel_frame COMMAND func_lockstep --states 4000
      --only VGAME.EXE:79BE)
    add_test(NAME matched_navigation_grid COMMAND func_lockstep --states 4000
      --only VGAME.EXE:8A90)
    add_test(NAME matched_armament_screen COMMAND func_lockstep --states 4000
      --only START.EXE:7EE)
    add_test(NAME matched_objectives_panel COMMAND func_lockstep --states 4000
      --only VGAME.EXE:9515)
    add_test(NAME matched_objective_place_alias COMMAND func_lockstep --states 4000
      --only VGAME.EXE:4B8F --seed 0x376cc4116689)
    add_test(NAME matched_segment_bindings COMMAND func_lockstep --states 4000
      --only VGAME.EXE:2)
    add_test(NAME matched_sprite_recolour COMMAND func_lockstep --states 4000
      --only MPS_LOGO.EXE:2)
    add_test(NAME matched_debrief_narrative COMMAND func_lockstep --states 4000
      --only END.EXE:2BEC)
    add_test(NAME matched_pilot_options COMMAND func_lockstep --states 4000
      --only START.EXE:6DB8)
    add_test(NAME matched_menu_hit_wrap COMMAND func_lockstep --states 4000 --seed 0xe77d9d194a06
      --only START.EXE:393D,END.EXE:1C3B)
    add_test(NAME matched_projectile_changed_return COMMAND func_lockstep --states 4000
      --seed 0x3ad6ecf4e53b --only VGAME.EXE:683E)
    # A fuzzed write altered the abs helper's code (EE0C), so a RET n is reached in the middle of another
    # instruction at the wrong stack depth while its own bytes are original (state 1799 at this seed).
    add_test(NAME matched_projectile_misaligned_return COMMAND func_lockstep --states 5000
      --seed 0x904ceaac77b9 --only VGAME.EXE:683E)
    add_test(NAME matched_projectile_stub_overwrite COMMAND func_lockstep --states 4000 --seed 0x1894deb34178
      --only VGAME.EXE:683E)
    add_test(NAME matched_set_pointer_alias COMMAND func_lockstep --states 5000 --seed 0x7721a2f2bd17
      --only START.EXE:3E84,END.EXE:2049)
    add_test(NAME matched_menu_input COMMAND func_lockstep --states 5000
      --only START.EXE:39F7,END.EXE:1CF5)
    add_test(NAME matched_horizon_and_record COMMAND func_lockstep --states 5000
      --only VGAME.EXE:1450,START.EXE:7904)
    add_test(NAME matched_scene_walk COMMAND func_lockstep --states 5000
      --only VGAME.EXE:4E6)
    add_test(NAME matched_compose_walk COMMAND func_lockstep --states 5000
      --only VGAME.EXE:DC46)
    add_test(NAME matched_start_brief_key_loop COMMAND func_lockstep --states 5000
      --only START.EXE:FA3)
    add_test(NAME matched_vgame_traffic_lock_and_fire COMMAND func_lockstep --states 5000
      --only VGAME.EXE:63F2)
    add_test(NAME matched_vgame_ground_site_engage COMMAND func_lockstep --states 5000
      --only VGAME.EXE:528D)
    add_test(NAME matched_start_draw_pilot_row COMMAND func_lockstep --states 5000
      --only START.EXE:6861)
    add_test(NAME matched_vgame_nearest_object_lookup COMMAND func_lockstep --states 5000
      --only VGAME.EXE:D14)
    add_test(NAME matched_vgame_attitude_indicator_draw COMMAND func_lockstep --states 5000
      --only VGAME.EXE:17B4)
    add_test(NAME matched_start_roster_event_loop COMMAND func_lockstep --states 5000
      --only START.EXE:623E)
    add_test(NAME matched_player_stream_picture_file COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:244)
    add_test(NAME matched_vgame_load_table_pair COMMAND func_lockstep --states 5000
      --only VGAME.EXE:9A0)
    add_test(NAME matched_start_arms_screen_loop COMMAND func_lockstep --states 5000
      --only START.EXE:963)
    add_test(NAME matched_start_sopen_file COMMAND func_lockstep --states 5000
      --only START.EXE:A59A)
    add_test(NAME matched_vgame_open_file_handle COMMAND func_lockstep --states 5000
      --only VGAME.EXE:F5F2)
    add_test(NAME matched_start_draw_pilot_card COMMAND func_lockstep --states 5000
      --only START.EXE:8D)
    add_test(NAME matched_vgame_draw_model_in_view COMMAND func_lockstep --states 5000
      --only VGAME.EXE:BB0D)
    add_test(NAME matched_start_draw_pilot_record_sheet COMMAND func_lockstep --states 5000
      --only START.EXE:64E5)
    add_test(NAME matched_start_world_page_objects COMMAND func_lockstep --states 5000
      --only START.EXE:74B1)
    add_test(NAME matched_end_replay_step COMMAND func_lockstep --states 5000
      --only END.EXE:714)
    add_test(NAME matched_end_wait_answer COMMAND func_lockstep --states 5000
      --only END.EXE:2092)
    add_test(NAME matched_start_mission_take_out_sites COMMAND func_lockstep --states 5000
      --only START.EXE:40F0)
    add_test(NAME matched_vgame_read_file_handle COMMAND func_lockstep --states 5000
      --only VGAME.EXE:F458)
    add_test(NAME matched_player_crt_setargv COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1602)
    add_test(NAME matched_vgame_model_edge_clip COMMAND func_lockstep --states 5000
      --only VGAME.EXE:33F)
    add_test(NAME matched_setup_crt_setargv COMMAND func_lockstep --states 5000
      --only SETUP.EXE:19B6)
    add_test(NAME matched_mps_logo_open_file COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:1894)
    add_test(NAME matched_mps_logo_play_intro COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:72)
    add_test(NAME matched_mps_logo_crt_setargv COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:8CC)
    add_test(NAME matched_dswap_open_file COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:1D66)
    add_test(NAME matched_dswap_crt_setargv COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:117E)
    add_test(NAME matched_dswap_load_from_drives COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:10)
    add_test(NAME matched_player_prepare_playback COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:5C)
    add_test(NAME matched_vgame_load_file_buffer COMMAND func_lockstep --states 5000
      --only VGAME.EXE:E586)
    add_test(NAME matched_vgame_clip_raster_line COMMAND func_lockstep --states 5000
      --only VGAME.EXE:330)
    add_test(NAME matched_start_launch_animation COMMAND func_lockstep --states 5000
      --only START.EXE:448)
    add_test(NAME matched_start_wait_key_or_button COMMAND func_lockstep --states 5000
      --only START.EXE:3F33)
    add_test(NAME matched_start_decode_mission_orders COMMAND func_lockstep --states 5000
      --only START.EXE:1308)
    add_test(NAME matched_start_fade_palette COMMAND func_lockstep --states 5000
      --only START.EXE:30A0)
    add_test(NAME matched_vgame_load_tile_objects COMMAND func_lockstep --states 5000
      --only VGAME.EXE:B05)
    add_test(NAME matched_vgame_model_fill_stream COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CE9)
    add_test(NAME matched_vgame_map_redraw COMMAND func_lockstep --states 5000
      --only VGAME.EXE:8462)
    add_test(NAME matched_vgame_scene_dispatch COMMAND func_lockstep --states 5000
      --only VGAME.EXE:3CE)
    add_test(NAME matched_end_debrief_open COMMAND func_lockstep --states 5000
      --only END.EXE:2A4)
    add_test(NAME matched_end_fade_palette COMMAND func_lockstep --states 5000
      --only END.EXE:1607)
    add_test(NAME matched_player_unpack_lz91_block COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:BE2)
    add_test(NAME matched_player_make_room_for_block COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:3FE)
    add_test(NAME matched_setup_load_overlay COMMAND func_lockstep --states 5000
      --only SETUP.EXE:A9F)
    add_test(NAME matched_end_draw_mission_route COMMAND func_lockstep --states 5000
      --only END.EXE:4D2)
    add_test(NAME matched_end_prepare_graphics_regions COMMAND func_lockstep --states 5000
      --only END.EXE:3D4F)
    add_test(NAME matched_vgame_configure_viewport COMMAND func_lockstep --states 5000
      --only VGAME.EXE:D8AF)
    add_test(NAME matched_vgame_set_cockpit_element COMMAND func_lockstep --states 5000
      --only VGAME.EXE:D578)
    add_test(NAME matched_start_read_file_handle COMMAND func_lockstep --states 5000
      --only START.EXE:A230)
    add_test(NAME matched_logo_read_file_handle COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:1510)
    add_test(NAME matched_mps_logo_fclose COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:2BA)
    add_test(NAME matched_vgame_line_in_box_window COMMAND func_lockstep --states 5000
      --only VGAME.EXE:70)
    add_test(NAME matched_start_graphics_pages_setup COMMAND func_lockstep --states 5000
      --only START.EXE:73CE)
    add_test(NAME matched_start_roster_settle_and_save COMMAND func_lockstep --states 5000
      --only START.EXE:6D07)
    add_test(NAME matched_start_draw_roster_board COMMAND func_lockstep --states 5000
      --only START.EXE:615C)
    add_test(NAME matched_mps_logo_draw_sprite_frame COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:5BE)
    add_test(NAME matched_vgame_armament_background COMMAND func_lockstep --states 5000
      --only VGAME.EXE:300)
    add_test(NAME matched_logo_read_stream_items COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:3B6)
    add_test(NAME matched_dswap_openfile COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:1520)
    add_test(NAME matched_mps_logo_openfile COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:D72)
    add_test(NAME matched_mps_logo_fread_far COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:116)
    add_test(NAME matched_logo_load_overlay COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:352)
    add_test(NAME matched_vgame_keys_and_signature COMMAND func_lockstep --states 5000
      --only VGAME.EXE:450A)
    add_test(NAME matched_start_launch_prompt COMMAND func_lockstep --states 5000
      --only START.EXE:38C)
    add_test(NAME matched_logo_fill_stream_buffer COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:BCC)
    add_test(NAME matched_player_frame_tick COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:738)
    # At this seed the fixture's random draw never planted the busy word, so the
    # busy early exit went unreached and the coverage floor failed.
    add_test(NAME matched_player_frame_tick_busy_seed COMMAND func_lockstep --states 4000
      --seed 0x70ca1542de1f --only PLAYER.EXE:738)
    add_test(NAME matched_start_write_file_handle COMMAND func_lockstep --states 5000
      --only START.EXE:A31A)
    add_test(NAME matched_start_cinit COMMAND func_lockstep --states 5000
      --only START.EXE:8FC6)
    add_test(NAME matched_end_cinit COMMAND func_lockstep --states 5000
      --only END.EXE:4EBC)
    add_test(NAME matched_mps_logo_cinit COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:10E)
    add_test(NAME matched_start_modal_ask COMMAND func_lockstep --states 5000
      --only START.EXE:3CEA)
    add_test(NAME matched_start_roster_save COMMAND func_lockstep --states 5000
      --only START.EXE:7272)
    add_test(NAME matched_start_frontend_menu COMMAND func_lockstep --states 5000
      --only START.EXE:20F)
    add_test(NAME matched_player_poll_controls COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:696)
    add_test(NAME matched_vgame_flight_start COMMAND func_lockstep --states 5000
      --only VGAME.EXE:F0)
    add_test(NAME matched_vgame_hud_bitmap_recolour COMMAND func_lockstep --states 5000
      --only VGAME.EXE:1C1)
    add_test(NAME matched_start_world_options COMMAND func_lockstep --states 5000
      --only START.EXE:5BF0)
    add_test(NAME matched_vgame_display_select COMMAND func_lockstep --states 5000
      --only VGAME.EXE:825D)
    add_test(NAME matched_start_objective_strike_line COMMAND func_lockstep --states 5000
      --only START.EXE:2696)
    add_test(NAME matched_vgame_release_store COMMAND func_lockstep --states 5000
      --only VGAME.EXE:7663)
    add_test(NAME matched_start_format_integer COMMAND func_lockstep --states 5000
      --only START.EXE:A015)
    add_test(NAME matched_vgame_key_bay_doors_and_tail COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C9AE)
    add_test(NAME matched_start_format_string_field COMMAND func_lockstep --states 5000
      --only START.EXE:9EB3)
    add_test(NAME matched_end_format_string_length COMMAND func_lockstep --states 5000
      --only END.EXE:5825)
    add_test(NAME matched_end_landed_screen COMMAND func_lockstep --states 5000
      --only END.EXE:3887)
    add_test(NAME matched_end_icon_place_draw COMMAND func_lockstep --states 5000
      --only END.EXE:99D)
    add_test(NAME matched_vgame_compose_defer_model COMMAND func_lockstep --states 5000
      --only VGAME.EXE:D9FE)
    add_test(NAME matched_start_roster_hover_detail COMMAND func_lockstep --states 5000
      --only START.EXE:644A)
    add_test(NAME matched_end_landing_fade_rates COMMAND func_lockstep --states 5000
      --only END.EXE:372F)
    add_test(NAME matched_end_format_integer COMMAND func_lockstep --states 5000
      --only END.EXE:5987)
    add_test(NAME matched_end_debrief_score_step COMMAND func_lockstep --states 5000
      --only END.EXE:34EA)
    add_test(NAME matched_end_promotion_announce COMMAND func_lockstep --states 5000
      --only END.EXE:2A01)
    add_test(NAME matched_vgame_release_place COMMAND func_lockstep --states 5000
      --only VGAME.EXE:4657)
    add_test(NAME matched_player_load_palette COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:D0C)
    add_test(NAME matched_end_draw_event_caption COMMAND func_lockstep --states 5000
      --only END.EXE:A98)
    add_test(NAME matched_vgame_interrupt_call COMMAND func_lockstep --states 5000
      --only VGAME.EXE:ED10)
    add_test(NAME matched_player_timer_calibrate COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:272)
    add_test(NAME matched_start_orders_folder COMMAND func_lockstep --states 5000
      --only START.EXE:20D6)
    add_test(NAME matched_end_timer_calibrate COMMAND func_lockstep --states 5000
      --only END.EXE:4CBC)
    add_test(NAME matched_end_show_picture COMMAND func_lockstep --states 5000
      --only END.EXE:1901)
    add_test(NAME matched_start_wait_table_event COMMAND func_lockstep --states 5000
      --only START.EXE:38AC)
    add_test(NAME matched_vgame_mission_cockpit_setup COMMAND func_lockstep --states 5000
      --only VGAME.EXE:4973)
    add_test(NAME matched_start_timer_calibrate COMMAND func_lockstep --states 5000
      --only START.EXE:8DD8)
    add_test(NAME matched_vgame_flight_session COMMAND func_lockstep --states 5000
      --only VGAME.EXE:235)
    add_test(NAME matched_start_draw_briefing_page COMMAND func_lockstep --states 5000
      --only START.EXE:141D)
    add_test(NAME matched_start_arms_hover_change COMMAND func_lockstep --states 5000
      --only START.EXE:B49)
    add_test(NAME matched_end_graphics_pages_setup COMMAND func_lockstep --states 5000
      --only END.EXE:3CD0)
    add_test(NAME matched_end_unpack_picture COMMAND func_lockstep --states 5000
      --only END.EXE:4722)
    add_test(NAME matched_start_leave_front_end COMMAND func_lockstep --states 5000
      --only START.EXE:76D1)
    add_test(NAME matched_start_frontend_startup COMMAND func_lockstep --states 5000
      --only START.EXE:7312)
    add_test(NAME matched_vgame_interrupt_call_regs COMMAND func_lockstep --states 5000
      --only VGAME.EXE:EC12)
    add_test(NAME matched_player_interrupt_call COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:13CE)
    add_test(NAME matched_player_show_picture COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:7EA)
    add_test(NAME matched_end_leave_program COMMAND func_lockstep --states 5000
      --only END.EXE:3F44)
    add_test(NAME matched_start_picture_load COMMAND func_lockstep --states 5000
      --only START.EXE:8782)
    add_test(NAME matched_player_play_out_and_finish COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:19A)
    add_test(NAME matched_end_pic_rows_to_page COMMAND func_lockstep --states 5000
      --only END.EXE:4648)
    add_test(NAME matched_vgame_fill_box COMMAND func_lockstep --states 5000
      --only VGAME.EXE:246)
    add_test(NAME matched_start_exit COMMAND func_lockstep --states 5000
      --only START.EXE:9094)
    add_test(NAME matched_start_edit_pilot_name COMMAND func_lockstep --states 5000
      --only START.EXE:6AB3)
    add_test(NAME matched_end_announce_awards COMMAND func_lockstep --states 5000
      --only END.EXE:2703)
    add_test(NAME matched_start_frontend_main COMMAND func_lockstep --states 5000
      --only START.EXE:10)
    add_test(NAME matched_end_debrief_main COMMAND func_lockstep --states 5000
      --only END.EXE:10)
    add_test(NAME matched_end_prepare_debrief COMMAND func_lockstep --states 5000
      --only END.EXE:3BF4)
    add_test(NAME matched_setup_timer_calibrate COMMAND func_lockstep --states 5000
      --only SETUP.EXE:111A)
    add_test(NAME matched_end_run_awards_pages COMMAND func_lockstep --states 5000
      --only END.EXE:2265)
    add_test(NAME matched_end_wait_report_widget COMMAND func_lockstep --states 5000
      --only END.EXE:1BAA)
    add_test(NAME matched_start_wait_confirm_cancel COMMAND func_lockstep --states 5000
      --only START.EXE:3EA5)
    add_test(NAME matched_vgame_pic_rows_to_page_far COMMAND func_lockstep --states 5000
      --only VGAME.EXE:A)
    add_test(NAME matched_start_onc_grid_reference COMMAND func_lockstep --states 5000
      --only START.EXE:58DF)
    add_test(NAME matched_end_awards_page_callback COMMAND func_lockstep --states 5000
      --only END.EXE:2200)
    add_test(NAME matched_end_collect_award_ribbons COMMAND func_lockstep --states 5000
      --only END.EXE:22EB)
    add_test(NAME matched_end_debrief_log_score COMMAND func_lockstep --states 5000
      --only END.EXE:3021)
    add_test(NAME matched_vgame_picture_open_far COMMAND func_lockstep --states 5000
      --only VGAME.EXE:8)
    add_test(NAME matched_end_report_page_text COMMAND func_lockstep --states 5000
      --only END.EXE:26A9)
    add_test(NAME matched_vgame_set_view_and_sky COMMAND func_lockstep --states 5000
      --only VGAME.EXE:D826)
    add_test(NAME matched_start_format_dispatch COMMAND func_lockstep --states 5000
      --only START.EXE:9E3E)
    add_test(NAME matched_start_briefing_page_setup COMMAND func_lockstep --states 5000
      --only START.EXE:F4E)
    add_test(NAME matched_start_load_picture_for_page COMMAND func_lockstep --states 5000
      --only START.EXE:35FC)
    add_test(NAME matched_end_wait_ticks_with_callback COMMAND func_lockstep --states 5000
      --only END.EXE:1708)
    add_test(NAME matched_start_wait_ticks_with_callback COMMAND func_lockstep --states 5000
      --only START.EXE:31AD)
    add_test(NAME matched_vgame_driver_call_scaled_args COMMAND func_lockstep --states 5000
      --only VGAME.EXE:1A98)
    add_test(NAME matched_vgame_driver_two_calls_far COMMAND func_lockstep --states 5000
      --only VGAME.EXE:2FF)
    add_test(NAME matched_vgame_sky_horizon_line COMMAND func_lockstep --states 5000
      --only VGAME.EXE:12)
    add_test(NAME matched_vgame_key_arm_select COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CE65)
    add_test(NAME matched_vgame_key_cycle_panel_state COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CD7E)
    add_test(NAME matched_vgame_display_pass COMMAND func_lockstep --states 5000
      --only VGAME.EXE:9EFA)
    add_test(NAME matched_vgame_navigation_display COMMAND func_lockstep --states 5000
      --only VGAME.EXE:8F71)
    add_test(NAME matched_vgame_armament_panel_refresh COMMAND func_lockstep --states 5000
      --only VGAME.EXE:93E9)
    add_test(NAME matched_start_site_popup_text COMMAND func_lockstep --states 5000
      --only START.EXE:1C2F)
    add_test(NAME matched_start_map_hover_light COMMAND func_lockstep --states 5000
      --only START.EXE:11DB)
    add_test(NAME matched_vgame_key_eject COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CD04)
    add_test(NAME matched_end_icon_frame_switch COMMAND func_lockstep --states 5000
      --only END.EXE:977)
    add_test(NAME matched_end_capture_page COMMAND func_lockstep --states 5000
      --only END.EXE:3BBF)
    add_test(NAME matched_end_interrupt_call COMMAND func_lockstep --states 5000
      --only END.EXE:519E)
    add_test(NAME matched_end_quit_confirm_panel COMMAND func_lockstep --states 5000
      --only END.EXE:3E61)
    add_test(NAME matched_start_call_interrupt COMMAND func_lockstep --states 5000
      --only START.EXE:9520)
    add_test(NAME matched_start_quit_confirmation COMMAND func_lockstep --states 5000
      --only START.EXE:7611)
    add_test(NAME matched_vgame_key_dispatcher_entry COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C8CA)
    add_test(NAME matched_vgame_key_toggle_display_flag COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C8E6)
    add_test(NAME matched_vgame_key_zoom_view COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C93A)
    add_test(NAME matched_vgame_key_unzoom_view COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C93F)
    add_test(NAME matched_vgame_key_ecm_switch COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C944)
    add_test(NAME matched_vgame_key_ir_jammer_switch COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C961)
    add_test(NAME matched_vgame_key_release_flare COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C968)
    add_test(NAME matched_vgame_key_release_chaff COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C96F)
    add_test(NAME matched_vgame_key_release_decoy COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C973)
    add_test(NAME matched_vgame_key_gear COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C977)
    add_test(NAME matched_vgame_key_flaps COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C993)
    add_test(NAME matched_vgame_key_scene_detail_switch COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C9BE)
    add_test(NAME matched_vgame_key_display_option_h COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C9C6)
    add_test(NAME matched_vgame_key_option_w COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C9CE)
    add_test(NAME matched_vgame_key_external_readouts COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C9D6)
    add_test(NAME matched_vgame_key_cockpit_height COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C9DE)
    add_test(NAME matched_vgame_key_sensitivity COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CA50)
    add_test(NAME matched_vgame_key_frame_rate_text COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CA8E)
    add_test(NAME matched_vgame_key_faster_time COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CAC7)
    add_test(NAME matched_vgame_key_normal_time COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CAE4)
    add_test(NAME matched_vgame_key_sound_level COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CAEA)
    add_test(NAME matched_vgame_key_training_switch COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CB27)
    add_test(NAME matched_vgame_key_next_weapon COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CB42)
    add_test(NAME matched_vgame_key_mark_displays COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CB95)
    add_test(NAME matched_vgame_key_step_hud_declutter COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CBA8)
    add_test(NAME matched_vgame_key_press_cannon_button COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CBBE)
    add_test(NAME matched_vgame_key_press_release_button COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CBD5)
    add_test(NAME matched_vgame_key_view_front COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CBDE)
    add_test(NAME matched_vgame_key_view_44 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CBE7)
    add_test(NAME matched_vgame_key_view_41 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CBF0)
    add_test(NAME matched_vgame_key_view_43 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CBF9)
    add_test(NAME matched_vgame_key_view_42 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CC02)
    add_test(NAME matched_vgame_key_camera_angle_left COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CC0B)
    add_test(NAME matched_vgame_key_camera_angle_forward COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CC12)
    add_test(NAME matched_vgame_key_camera_angle_right COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CC19)
    add_test(NAME matched_vgame_key_camera_angle_rear COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CC20)
    add_test(NAME matched_vgame_key_select_camera_view COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CC25)
    add_test(NAME matched_vgame_key_camera_level_down COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CC4C)
    add_test(NAME matched_vgame_key_camera_level_up COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CC93)
    add_test(NAME matched_vgame_key_view_84 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CCCE)
    add_test(NAME matched_vgame_key_view_85 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CCD7)
    add_test(NAME matched_vgame_key_view_87 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CCE0)
    add_test(NAME matched_vgame_key_view_89 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CCE9)
    add_test(NAME matched_vgame_key_view_88 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CCF2)
    add_test(NAME matched_vgame_key_view_8b COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CCFB)
    add_test(NAME matched_vgame_key_select_left_display COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CDB8)
    add_test(NAME matched_vgame_key_toggle_hud_colour COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CDCB)
    add_test(NAME matched_vgame_key_show_display_15 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CDD6)
    add_test(NAME matched_vgame_key_toggle_lamp_25 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CDDB)
    add_test(NAME matched_vgame_key_navigation_second_set COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CDE9)
    add_test(NAME matched_vgame_key_navigation_first_set COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CDF3)
    add_test(NAME matched_vgame_key_toggle_ils COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CE55)
    add_test(NAME matched_vgame_key_show_display_18 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CE5D)
    add_test(NAME matched_vgame_key_detail_level COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CA0A)
    add_test(NAME matched_vgame_key_toggle_height_hold COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CB77)
    add_test(NAME matched_vgame_key_reset_waypoints COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CE01)
    add_test(NAME matched_dswap_string_to_long COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:1BD0)
    add_test(NAME matched_setup_set_cursor COMMAND func_lockstep --states 5000
      --only SETUP.EXE:512)
    add_test(NAME matched_setup_shut_down COMMAND func_lockstep --states 5000
      --only SETUP.EXE:53B)
    add_test(NAME matched_setup_wait_ticks COMMAND func_lockstep --states 5000
      --only SETUP.EXE:A4B)
    add_test(NAME matched_vgame_register_display_pages COMMAND func_lockstep --states 5000
      --only VGAME.EXE:2C4)
    add_test(NAME matched_vgame_background_span_fill COMMAND func_lockstep --states 5000
      --only VGAME.EXE:4FE)
    add_test(NAME matched_vgame_picture_close_far COMMAND func_lockstep --states 5000
      --only VGAME.EXE:C4)
    add_test(NAME matched_vgame_picture_read_block_far COMMAND func_lockstep --states 5000
      --only VGAME.EXE:153)
    add_test(NAME matched_vgame_frame_loop COMMAND func_lockstep --states 5000
      --only VGAME.EXE:1BCE)
    add_test(NAME matched_end_read_file_to_far_buffer COMMAND func_lockstep --states 5000
      --only END.EXE:FA4)
    add_test(NAME matched_end_read_file_to_near_buffer COMMAND func_lockstep --states 5000
      --only END.EXE:FDA)
    add_test(NAME matched_end_allocate_or_quit COMMAND func_lockstep --states 5000
      --only END.EXE:2BA0)
    add_test(NAME matched_end_show_debrief_picture COMMAND func_lockstep --states 5000
      --only END.EXE:370A)
    add_test(NAME matched_end_dos_open_or_report COMMAND func_lockstep --states 5000
      --only END.EXE:400F)
    add_test(NAME matched_end_exit COMMAND func_lockstep --states 5000
      --only END.EXE:4F8A)
    add_test(NAME matched_start_hangar_scene COMMAND func_lockstep --states 5000
      --only START.EXE:311)
    add_test(NAME matched_start_objective_beacon_drop COMMAND func_lockstep --states 5000
      --only START.EXE:2760)
    add_test(NAME matched_start_objective_secret_airstrip COMMAND func_lockstep --states 5000
      --only START.EXE:277D)
    add_test(NAME matched_start_objective_coaler_route COMMAND func_lockstep --states 5000
      --only START.EXE:279A)
    add_test(NAME matched_start_objective_coaler_unknown_destination COMMAND func_lockstep --states 5000
      --only START.EXE:27CA)
    add_test(NAME matched_start_objective_tu95_intercept COMMAND func_lockstep --states 5000
      --only START.EXE:27E3)
    add_test(NAME matched_start_objective_fighter_patrol COMMAND func_lockstep --states 5000
      --only START.EXE:27F7)
    add_test(NAME matched_start_load_file_far COMMAND func_lockstep --states 5000
      --only START.EXE:2864)
    add_test(NAME matched_start_alloc_buffer COMMAND func_lockstep --states 5000
      --only START.EXE:40A4)
    add_test(NAME matched_start_install_clock_hook COMMAND func_lockstep --states 5000
      --only START.EXE:8CA8)
    add_test(NAME matched_start_long_to_text COMMAND func_lockstep --states 5000
      --only START.EXE:A458)
    add_test(NAME matched_start_malloc COMMAND func_lockstep --states 5000
      --only START.EXE:A768)
    add_test(NAME matched_mps_logo_write_palette COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:33A)
    add_test(NAME matched_mps_logo_read_palette COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:38E)
    add_test(NAME matched_mps_logo_far_alloc COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:BC)
    add_test(NAME matched_mps_logo_malloc COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:1B8B)
    add_test(NAME matched_player_hook_break_vectors COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:583)
    add_test(NAME matched_player_malloc COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:23B1)
    add_test(NAME matched_player_timer_install COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:A)
    add_test(NAME matched_player_clock_hook_step COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:336)
    add_test(NAME matched_setup_timer_hook_install COMMAND func_lockstep --states 5000
      --only SETUP.EXE:FC0)
    add_test(NAME matched_start_load_file COMMAND func_lockstep --states 5000
      --only START.EXE:289A)
    add_test(NAME matched_end_read_block_or_report COMMAND func_lockstep --states 5000
      --only END.EXE:4160)
    add_test(NAME matched_end_ribbon_draw_from_full_row COMMAND func_lockstep --states 5000
      --only END.EXE:2430)
    add_test(NAME matched_end_death_message_box_draw COMMAND func_lockstep --states 5000
      --only END.EXE:36D6)
    add_test(NAME matched_mps_logo_fread_bytes_far COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:30)
    add_test(NAME matched_player_finish COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1F6)
    add_test(NAME matched_dswap_dos_close COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:647)
    add_test(NAME matched_dswap_dos_read COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:668)
    add_test(NAME matched_dswap_read_picture_block_or_report COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:6EA)
    add_test(NAME matched_dswap_pic_rows_to_page COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:8F4)
    add_test(NAME matched_dswap_bios_disk_call COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:10F4)
    add_test(NAME matched_end_empty_routine_00c7 COMMAND func_lockstep --states 5000
      --only END.EXE:C7)
    add_test(NAME matched_end_empty_routine_00c8 COMMAND func_lockstep --states 5000
      --only END.EXE:C8)
    add_test(NAME matched_end_empty_routine_22ea COMMAND func_lockstep --states 5000
      --only END.EXE:22EA)
    add_test(NAME matched_end_empty_routine_47fe COMMAND func_lockstep --states 5000
      --only END.EXE:47FE)
    add_test(NAME matched_end_free_block_or_quit COMMAND func_lockstep --states 5000
      --only END.EXE:2BCB)
    add_test(NAME matched_end_load_color_registers COMMAND func_lockstep --states 5000
      --only END.EXE:42BE)
    add_test(NAME matched_end_set_mouse_cursor COMMAND func_lockstep --states 5000
      --only END.EXE:451B)
    add_test(NAME matched_player_format_string_field COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1C12)
    add_test(NAME matched_player_format_integer COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1D8D)
    add_test(NAME matched_player_abort COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1F54)
    add_test(NAME matched_setup_print_dollar_string COMMAND func_lockstep --states 5000
      --only SETUP.EXE:1509)
    add_test(NAME matched_start_empty_hook_057b COMMAND func_lockstep --states 5000
      --only START.EXE:57B)
    add_test(NAME matched_start_empty_hook_057c COMMAND func_lockstep --states 5000
      --only START.EXE:57C)
    add_test(NAME matched_start_empty_hook_885e COMMAND func_lockstep --states 5000
      --only START.EXE:885E)
    add_test(NAME matched_vgame_empty_hook_11d8_0155 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:155)
    add_test(NAME matched_vgame_empty_hook_02c2 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:2C2)
    add_test(NAME matched_vgame_empty_hook_02c3 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:2C3)
    add_test(NAME matched_vgame_run_flight COMMAND func_lockstep --states 5000
      --only VGAME.EXE:1BC2)
    add_test(NAME matched_mps_logo_string_position COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:3A)
    add_test(NAME matched_mps_logo_alloc_or_abort COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:17B0)
    add_test(NAME matched_mps_logo_dos_block_freed COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:102)
    add_test(NAME matched_mps_logo_malloc_jump COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:1A64)
    add_test(NAME matched_mps_logo_format_scan COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:FE4)
    add_test(NAME matched_mps_logo_write_file_handle COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:15FA)
    add_test(NAME matched_player_stack_check COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1FD0)
    add_test(NAME matched_player_raise_exit_code_three COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:22AF)
    add_test(NAME matched_end_format_decimal_arm COMMAND func_lockstep --states 5000
      --only END.EXE:5948)
    add_test(NAME matched_end_debrief_message_arm_0e90 COMMAND func_lockstep --states 5000
      --only END.EXE:3692)
    add_test(NAME matched_dswap_dos_open_or_report COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:599)
    add_test(NAME matched_dswap_load_color_registers COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:848)
    add_test(NAME matched_vgame_far_sine_entry COMMAND func_lockstep --states 5000
      --only VGAME.EXE:82)
    add_test(NAME matched_vgame_far_hud_colours_init COMMAND func_lockstep --states 5000
      --only VGAME.EXE:E)
    add_test(NAME matched_vgame_far_hud_pass COMMAND func_lockstep --states 5000
      --only VGAME.EXE:A)
    add_test(NAME matched_vgame_far_hud_layout_apply COMMAND func_lockstep --states 5000
      --only VGAME.EXE:12)
    add_test(NAME matched_player_format_dispatch COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1BB6)
    add_test(NAME matched_start_quiz_draw_screen COMMAND func_lockstep --states 5000
      --only START.EXE:57E)
    add_test(NAME matched_start_quiz_ask_and_judge COMMAND func_lockstep --states 5000
      --only START.EXE:703)
    add_test(NAME matched_start_quiz_hover_highlight COMMAND func_lockstep --states 5000
      --only START.EXE:786)
    add_test(NAME matched_dswap_fatal_text_exit COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:754)
    add_test(NAME matched_mps_logo_format_field COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:1364)
    add_test(NAME matched_vgame_message_and_key COMMAND func_lockstep --states 5000
      --only VGAME.EXE:CDD)
    add_test(NAME matched_end_fatal_error_screen COMMAND func_lockstep --states 5000
      --only END.EXE:41CA)
    add_test(NAME matched_dswap_show_swap_disk_screen COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:23E)
    add_test(NAME matched_dswap_show_picture_file COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:3A7)
    add_test(NAME matched_dswap_pic_start COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:AD5)
    add_test(NAME matched_dswap_pic_lzw COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:BCF)
    add_test(NAME matched_mps_logo_clear_text_screen COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:44)
    add_test(NAME matched_mps_logo_fatal_error COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:C)
    add_test(NAME matched_mps_logo_exit COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:1D3)
    add_test(NAME matched_end_load_report_picture COMMAND func_lockstep --states 5000
      --only END.EXE:3B9A)
    add_test(NAME matched_end_debrief_and_tally COMMAND func_lockstep --states 5000
      --only END.EXE:3D9)
    add_test(NAME matched_player_format_scan COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1A7E)
    add_test(NAME matched_player_fprintf COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:18C4)
    add_test(NAME matched_player_assert_fail COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1476)
    add_test(NAME matched_player_write_message COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1839)
    add_test(NAME matched_player_startup_check_failed COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:15BA)
    add_test(NAME matched_player_raise COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:223A)
    add_test(NAME matched_player_timer_program_count COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:244)
    add_test(NAME matched_player_main COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:10)
    add_test(NAME matched_misc_dos_read_char_echo COMMAND func_lockstep --states 5000
      --only MISC.EXE:0)
    add_test(NAME matched_misc_key_waiting COMMAND func_lockstep --states 5000
      --only MISC.EXE:A)
    add_test(NAME matched_misc_clear_shift_state COMMAND func_lockstep --states 5000
      --only MISC.EXE:18)
    add_test(NAME matched_mgraphic_font_char_width COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:23A)
    add_test(NAME matched_misc_joystick_button_pressed COMMAND func_lockstep --states 5000
      --only MISC.EXE:29)
    add_test(NAME matched_f117_load_overlay COMMAND func_lockstep --states 5000
      --only F117.COM:3CB)
    add_test(NAME matched_mgraphic_text_thunk_02a1 COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:203)
    add_test(NAME matched_mgraphic_text_thunk_02b0 COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:20E)
    add_test(NAME matched_mgraphic_text_thunk_0314 COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:219)
    add_test(NAME matched_mgraphic_text_thunk_0376 COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:224)
    add_test(NAME matched_mgraphic_text_thunk_03c9 COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:22F)
    add_test(NAME matched_mgraphic_text_frame_thunk_03c9 COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:19A)
    add_test(NAME matched_mgraphic_get_surface_segment COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:7C8)
    add_test(NAME matched_mgraphic_set_surface_segment COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:7C3)
    add_test(NAME matched_mgraphic_clear_row_offset COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:7CD)
    add_test(NAME matched_mgraphic_get_constant_segment COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:7E4)
    add_test(NAME matched_mgraphic_get_flag_byte COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:80F)
    add_test(NAME matched_mgraphic_table_word COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:12BA)
    add_test(NAME matched_mgraphic_pen_pixel_address COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:581)
    add_test(NAME matched_mgraphic_sprite_clip_reset COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:C5D)
    add_test(NAME matched_mgraphic_show_back_page COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:11F1)
    add_test(NAME matched_mgraphic_set_pen_colour COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:55B)
    add_test(NAME matched_mgraphic_set_data_word_00ce COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:7FE)
    add_test(NAME matched_mgraphic_set_row_offset COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:7D5)
    add_test(NAME matched_mgraphic_set_page_segment COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:791)
    add_test(NAME matched_mgraphic_row_column_offset COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:814)
    add_test(NAME matched_mgraphic_set_shake_count COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:1251)
    add_test(NAME matched_mgraphic_copy_row_from_stack COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:12E0)
    add_test(NAME matched_mgraphic_blit_transparent COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:EE6)
    add_test(NAME matched_mgraphic_blit_transparent_entry COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:ED5)
    add_test(NAME matched_mgraphic_page_copy_rect COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:1131)
    add_test(NAME matched_mgraphic_select_page COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:7AC)
    add_test(NAME matched_mgraphic_copy_text_var_194 COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:7A3)
    add_test(NAME matched_mgraphic_select_overlay_seg COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:12D5)
    add_test(NAME matched_mgraphic_ret_12dd COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:12DD)
    add_test(NAME matched_mgraphic_ret_12df COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:12DF)
    add_test(NAME matched_mgraphic_clip_blit_entry COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:E30)
    add_test(NAME matched_mgraphic_clip_rect COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:E41)
    add_test(NAME matched_mgraphic_largest_free_block COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:4A4)
    add_test(NAME matched_mgraphic_enter_mode_13h COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:4B6)
    add_test(NAME matched_mgraphic_screen_off COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:4CA)
    add_test(NAME matched_mgraphic_screen_on COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:4E4)
    add_test(NAME matched_mgraphic_alloc_or_page_segment COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:4FE)
    add_test(NAME matched_mgraphic_plot_pen_pixel_tail COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:57A)
    add_test(NAME matched_mgraphic_ret_constant_3 COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:826)
    add_test(NAME matched_mgraphic_arena_take_largest_block COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:9A8)
    add_test(NAME matched_mgraphic_clear_es_segment COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:1118)
    add_test(NAME matched_mgraphic_draw_dashed_rows COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:71A)
    add_test(NAME matched_mgraphic_compile_bitmap_rows COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:B0C)
    add_test(NAME matched_mgraphic_replace_colour_in_rect COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:1188)
    add_test(NAME matched_mgraphic_palette_flash_and_screen_jolt COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:125C)
    add_test(NAME matched_mgraphic_arena_release COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:9D4)
    add_test(NAME matched_mgraphic_fill_spans COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:F3D)
    add_test(NAME matched_mgraphic_draw_clipped_sprite COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:B70)
    add_test(NAME matched_mgraphic_draw_scaled_sprite COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:C95)
    add_test(NAME matched_mgraphic_draw_line COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:599)
    add_test(NAME matched_mgraphic_bitmap_buffer_open COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:A02)
    add_test(NAME matched_mgraphic_bitmap_buffer_close COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:A4D)
    add_test(NAME matched_mgraphic_bitmap_encode_row COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:A6A)
    add_test(NAME matched_mgraphic_clip_sprite_bounds COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:BC0)
    add_test(NAME matched_mgraphic_draw_font_string COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:61A)
    add_test(NAME matched_mgraphic_fill_spans_mode_zero COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:F3B)
    add_test(NAME matched_mgraphic_text_clip_left COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:2B9)
    add_test(NAME matched_mgraphic_text_window_clip_bytes COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:37F)
    add_test(NAME matched_mgraphic_text_return_tail_restore_byte COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:3D1)
    add_test(NAME matched_mgraphic_text_begin_font_string COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:3E8)
    add_test(NAME matched_mgraphic_text_front_font_string COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:3C9)
    add_test(NAME matched_mgraphic_text_clip_right COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:31D)
    add_test(NAME matched_mgraphic_text_front_clipped_font_string COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:376)
    add_test(NAME matched_mgraphic_text_front_clipped_string COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:2A1)
    add_test(NAME matched_mgraphic_text_front_left_clipped_font_string COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:2B0)
    add_test(NAME matched_mgraphic_text_front_right_clipped_font_string COMMAND func_lockstep --states 5000
      --only MGRAPHIC.EXE:314)
    add_test(NAME matched_start_selection_change COMMAND func_lockstep --states 5000
      --only START.EXE:5E6F)
    add_test(NAME matched_start_theatre_page_open COMMAND func_lockstep --states 5000
      --only START.EXE:5A8A)
    add_test(NAME matched_start_draw_message_panel COMMAND func_lockstep --states 5000
      --only START.EXE:6045)
    add_test(NAME matched_start_draw_panel_rectangles COMMAND func_lockstep --states 5000
      --only START.EXE:5FFA)
    add_test(NAME matched_dswap_malloc COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:2097)
    add_test(NAME matched_setup_malloc COMMAND func_lockstep --states 5000
      --only SETUP.EXE:1D69)
    add_test(NAME matched_player_get_video_mode COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:520)
    add_test(NAME matched_mps_logo_format_next_pointer COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:1403)
    add_test(NAME matched_mps_logo_check_null_region COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:8A8)
    add_test(NAME matched_start_free_page_or_exit COMMAND func_lockstep --states 5000
      --only START.EXE:40CF)
    add_test(NAME matched_player_set_interrupt_vector COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:14CA)
    add_test(NAME matched_mps_logo_set_video_mode COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:3E4)
    add_test(NAME matched_player_get_interrupt_vector COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:14B8)
    add_test(NAME matched_player_close_picture_handle COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:BCD)
    add_test(NAME matched_dswap_current_drive_one_based COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:10D0)
    add_test(NAME matched_start_position_mouse_pointer COMMAND func_lockstep --states 5000
      --only START.EXE:85A7)
    add_test(NAME matched_setup_advance_calibration_counters COMMAND func_lockstep --states 5000
      --only SETUP.EXE:A8C)
    add_test(NAME matched_start_bios_tick_low COMMAND func_lockstep --states 5000
      --only START.EXE:8607)
    add_test(NAME matched_start_flush_all_count COMMAND func_lockstep --states 5000
      --only START.EXE:9CA4)
    add_test(NAME matched_end_bios_tick_low COMMAND func_lockstep --states 5000
      --only END.EXE:45A6)
    add_test(NAME matched_mps_logo_format_next_word COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:13EF)
    add_test(NAME matched_dswap_flush_all_count COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:1D14)
    add_test(NAME matched_start_cel_steps_remaining COMMAND func_lockstep --states 5000
      --only START.EXE:2A7E)
    add_test(NAME matched_misc_read_key COMMAND func_lockstep --states 5000
      --only MISC.EXE:5)
    add_test(NAME matched_dswap_picture_noop COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:AAA)
    add_test(NAME matched_player_picture_end_flag COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:BDC)
    add_test(NAME matched_rsound_log_private_stack_tick COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:21B)
    add_test(NAME matched_asound_log_timer_tick_service COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:344)
    add_test(NAME matched_isound_speaker_tick COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:253)
    add_test(NAME matched_asound_log_voice_record_start COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:515)
    add_test(NAME matched_asound_log_reset_voice_records COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:58F)
    add_test(NAME matched_asound_log_write_operator_level COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:A1C)
    add_test(NAME matched_asound_log_write_operator_flags COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:B30)
    add_test(NAME matched_asound_log_noise_step COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:333)
    add_test(NAME matched_asound_117_noise_step COMMAND func_lockstep --states 5000
      --only ASOUND.117:3C4)
    add_test(NAME matched_isound_reset_sequencer_and_speaker_off COMMAND func_lockstep --states 5000
      --only ISOUND.117:163)
    add_test(NAME matched_isound_start_song_state COMMAND func_lockstep --states 5000
      --only ISOUND.117:140)
    add_test(NAME matched_rsound_log_reset_sequencer_state COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:171)
    add_test(NAME matched_rsound_log_init_voice_record COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:40C)
    add_test(NAME matched_rsound_117_reset_sequencer_records COMMAND func_lockstep --states 5000
      --only RSOUND.117:B42)
    add_test(NAME matched_rsound_117_random_step COMMAND func_lockstep --states 5000
      --only RSOUND.117:B9C)
    add_test(NAME matched_rsound_log_random_step COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:20A)
    add_test(NAME matched_rsound_log_update_six_voice_records COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:FA2)
    add_test(NAME matched_rsound_log_init_once COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:2BE)
    add_test(NAME matched_rsound_log_reset_and_program_nine_voices COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:580)
    add_test(NAME matched_isound_log_speaker_noise_vibrato_tick COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:10B)
    add_test(NAME matched_isound_117_speaker_noise_vibrato_tick COMMAND func_lockstep --states 5000
      --only ISOUND.117:102)
    add_test(NAME matched_asound_117_voice_flags_union COMMAND func_lockstep --states 5000
      --only ASOUND.117:839)
    add_test(NAME matched_rsound_117_voice_flags_union COMMAND func_lockstep --states 5000
      --only RSOUND.117:E2A)
    add_test(NAME matched_isound_arm_effect_timer_flag COMMAND func_lockstep --states 5000
      --only ISOUND.117:5A4)
    add_test(NAME matched_isound_117_pair_union COMMAND func_lockstep --states 5000
      --only ISOUND.117:626)
    add_test(NAME matched_asound_117_arm_effect_flags COMMAND func_lockstep --states 5000
      --only ASOUND.117:5A5)
    add_test(NAME matched_isound_117_start_song_entry COMMAND func_lockstep --states 5000
      --only ISOUND.117:8)
    add_test(NAME matched_asound_log_chip_init COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:19A)
    add_test(NAME matched_asound_log_write_feedback_connection COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:A62)
    add_test(NAME matched_asound_log_write_attack_decay COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:AAA)
    add_test(NAME matched_asound_log_write_sustain_release COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:AEC)
    add_test(NAME matched_asound_log_write_depth_flag COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:A00)
    add_test(NAME matched_asound_log_write_key_off COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:7DC)
    add_test(NAME matched_asound_log_write_waveform_select COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:BB8)
    add_test(NAME matched_asound_log_write_rhythm_depth COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:9B6)
    add_test(NAME matched_asound_log_write_operator_level_scaled COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:640)
    add_test(NAME matched_asound_log_write_operator_pitch COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:6AA)
    add_test(NAME matched_asound_log_write_voice_operator_registers COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:BE2)
    add_test(NAME matched_asound_log_open_driver COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:5DA)
    add_test(NAME matched_asound_log_close_driver COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:618)
    add_test(NAME matched_asound_log_driver_function_dispatch COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:5F7)
    add_test(NAME matched_asound_117_write_voice_key_register COMMAND func_lockstep --states 5000
      --only ASOUND.117:E5E)
    add_test(NAME matched_asound_117_write_voice_pitch COMMAND func_lockstep --states 5000
      --only ASOUND.117:DCE)
    add_test(NAME matched_asound_117_write_voice_pitch_from_note COMMAND func_lockstep --states 5000
      --only ASOUND.117:D1C)
    add_test(NAME matched_asound_117_write_voice_level COMMAND func_lockstep --states 5000
      --only ASOUND.117:C2E)
    add_test(NAME matched_asound_117_set_clamped_rate COMMAND func_lockstep --states 5000
      --only ASOUND.117:3D8)
    add_test(NAME matched_asound_117_load_rate_argument COMMAND func_lockstep --states 5000
      --only ASOUND.117:3D5)
    add_test(NAME matched_asound_117_store_command_word_17e2 COMMAND func_lockstep --states 5000
      --only ASOUND.117:B90)
    add_test(NAME matched_asound_117_reset_voice_record COMMAND func_lockstep --states 5000
      --only ASOUND.117:5E7)
    add_test(NAME matched_asound_117_reset_record_190a COMMAND func_lockstep --states 5000
      --only ASOUND.117:5E3)
    add_test(NAME matched_asound_117_reset_record_1928 COMMAND func_lockstep --states 5000
      --only ASOUND.117:5DC)
    add_test(NAME matched_asound_117_reset_record_1946 COMMAND func_lockstep --states 5000
      --only ASOUND.117:5D5)
    add_test(NAME matched_asound_117_reset_record_1964 COMMAND func_lockstep --states 5000
      --only ASOUND.117:5CE)
    add_test(NAME matched_asound_117_reset_record_1982 COMMAND func_lockstep --states 5000
      --only ASOUND.117:5C7)
    add_test(NAME matched_asound_117_reset_record_19a0 COMMAND func_lockstep --states 5000
      --only ASOUND.117:5C0)
    add_test(NAME matched_rsound_117_start_voice_one COMMAND func_lockstep --states 5000
      --only RSOUND.117:EDB)
    add_test(NAME matched_rsound_117_start_voice_4_prefix COMMAND func_lockstep --states 5000
      --only RSOUND.117:EBD)
    add_test(NAME matched_rsound_117_start_voice_3_prefix COMMAND func_lockstep --states 5000
      --only RSOUND.117:EC7)
    add_test(NAME matched_rsound_117_start_voice_2_prefix COMMAND func_lockstep --states 5000
      --only RSOUND.117:ED1)
    add_test(NAME matched_rsound_117_send_note_on COMMAND func_lockstep --states 5000
      --only RSOUND.117:444)
    add_test(NAME matched_rsound_117_send_program_change COMMAND func_lockstep --states 5000
      --only RSOUND.117:46D)
    add_test(NAME matched_rsound_117_flush_pending_notes COMMAND func_lockstep --states 5000
      --only RSOUND.117:3D6)
    add_test(NAME matched_rsound_log_driver_function_dispatch COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:5F6)
    add_test(NAME matched_rsound_log_open_driver COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:5CC)
    add_test(NAME matched_rsound_log_close_driver COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:611)
    add_test(NAME matched_isound_log_start_song_entry COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:51)
    add_test(NAME matched_isound_117_driver_function_dispatch COMMAND func_lockstep --states 5000
      --only ISOUND.117:8E)
    add_test(NAME matched_isound_117_tick_and_take_pending COMMAND func_lockstep --states 5000
      --only ISOUND.117:B5)
    add_test(NAME matched_asound_log_install_timer COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:5A)
    add_test(NAME matched_isound_log_save_timer_vector COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:60E)
    add_test(NAME matched_rsound_log_install_timers COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:5B)
    add_test(NAME matched_rsound_log_timer_install_return COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:AD)
    add_test(NAME matched_asound_117_command_set_flag_17f6_on COMMAND func_lockstep --states 5000
      --only ASOUND.117:41E)
    add_test(NAME matched_asound_117_command_clear_flag_17f6_and_rate COMMAND func_lockstep --states 5000
      --only ASOUND.117:424)
    add_test(NAME matched_asound_117_command_reset_record_19a0_data_0e9c COMMAND func_lockstep --states 5000
      --only ASOUND.117:634)
    add_test(NAME matched_asound_117_command_reset_record_190a_data_0eb4 COMMAND func_lockstep --states 5000
      --only ASOUND.117:647)
    add_test(NAME matched_asound_117_command_reset_record_1946_data_0ee4 COMMAND func_lockstep --states 5000
      --only ASOUND.117:686)
    add_test(NAME matched_asound_117_store_cx_in_record_field_1a COMMAND func_lockstep --states 5000
      --only ASOUND.117:75D)
    add_test(NAME matched_asound_117_command_reset_record_1964_data_0f4a COMMAND func_lockstep --states 5000
      --only ASOUND.117:8B0)
    add_test(NAME matched_asound_117_bcd_to_binary_al COMMAND func_lockstep --states 5000
      --only ASOUND.117:BF4)
    add_test(NAME matched_isound_log_bcd_byte_to_binary COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:6E)
    add_test(NAME matched_isound_log_reset_state_and_speaker_off COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:161)
    add_test(NAME matched_isound_log_return_after_restore_sti COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:66C)
    add_test(NAME matched_rsound_117_send_channel_volume COMMAND func_lockstep --states 5000
      --only RSOUND.117:490)
    add_test(NAME matched_rsound_117_send_channel_pan COMMAND func_lockstep --states 5000
      --only RSOUND.117:4E1)
    add_test(NAME matched_rsound_117_start_voice_9_prefix COMMAND func_lockstep --states 5000
      --only RSOUND.117:E95)
    add_test(NAME matched_rsound_117_start_voice_5_prefix COMMAND func_lockstep --states 5000
      --only RSOUND.117:EB3)
    add_test(NAME matched_rsound_117_hook_interrupt_0a_once COMMAND func_lockstep --states 5000
      --only RSOUND.117:A6B)
    add_test(NAME matched_rsound_117_start_voice_5_with_default_time COMMAND func_lockstep --states 5000
      --only RSOUND.117:10CF)
    add_test(NAME matched_rsound_117_start_voice_1_random_note COMMAND func_lockstep --states 5000
      --only RSOUND.117:1119)
    add_test(NAME matched_rsound_117_start_mpu_driver COMMAND func_lockstep --states 5000
      --only RSOUND.117:11F9)
    add_test(NAME matched_rsound_117_take_pending_request COMMAND func_lockstep --states 5000
      --only RSOUND.117:1309)
    add_test(NAME matched_rsound_log_release_channel_notes COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:63C)
    add_test(NAME matched_rsound_log_send_note_on_message COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:6F4)
    add_test(NAME matched_rsound_log_send_program_change_message COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:754)
    add_test(NAME matched_rsound_log_send_pan_message COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:860)
    add_test(NAME matched_isound_log_reload_timer_speaker_rate COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:603)
    add_test(NAME matched_isound_log_return_after_install_sti COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:60D)
    add_test(NAME matched_isound_log_restore_timer_vector COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:635)
    add_test(NAME matched_isound_log_reload_timer_zero COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:663)
    add_test(NAME matched_asound_117_cmd_0899_reset_19a0 COMMAND func_lockstep --states 5000
      --only ASOUND.117:899)
    add_test(NAME matched_asound_117_cmd_0892_reset_190a COMMAND func_lockstep --states 5000
      --only ASOUND.117:892)
    add_test(NAME matched_asound_117_cmd_090e_reset_1946 COMMAND func_lockstep --states 5000
      --only ASOUND.117:90E)
    add_test(NAME matched_asound_117_cmd_08ad_reset_1964 COMMAND func_lockstep --states 5000
      --only ASOUND.117:8AD)
    add_test(NAME matched_asound_117_cmd_08bb_reset_1964 COMMAND func_lockstep --states 5000
      --only ASOUND.117:8BB)
    add_test(NAME matched_asound_117_cmd_088b_reset_1982 COMMAND func_lockstep --states 5000
      --only ASOUND.117:88B)
    add_test(NAME matched_asound_117_cmd_08d0_reset_190a COMMAND func_lockstep --states 5000
      --only ASOUND.117:8D0)
    add_test(NAME matched_asound_117_cmd_08c2_reset_19a0 COMMAND func_lockstep --states 5000
      --only ASOUND.117:8C2)
    add_test(NAME matched_rsound_log_uninstall_timers COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:AE)
    add_test(NAME matched_rsound_log_install_int8_timer_prefix COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:251)
    add_test(NAME matched_rsound_log_install_timer_reload COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:27C)
    add_test(NAME matched_rsound_log_install_timer_restore_and_enable COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:289)
    add_test(NAME matched_rsound_log_install_timer_return COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:28D)
    add_test(NAME matched_isound_117_bcd_byte_to_binary COMMAND func_lockstep --states 5000
      --only ISOUND.117:65)
    add_test(NAME matched_isound_117_arm_effect_flag_ff COMMAND func_lockstep --states 5000
      --only ISOUND.117:5B7)
    add_test(NAME matched_asound_117_dsp_write_when_ready COMMAND func_lockstep --states 5000
      --only ASOUND.117:1929)
    add_test(NAME matched_asound_117_dsp_read_when_ready COMMAND func_lockstep --states 5000
      --only ASOUND.117:193E)
    add_test(NAME matched_asound_117_adlib_write_register_slow COMMAND func_lockstep --states 5000
      --only ASOUND.117:33A)
    add_test(NAME matched_asound_117_speaker_timer_start_install COMMAND func_lockstep --states 5000
      --only ASOUND.117:B57)
    add_test(NAME matched_asound_117_command_0887_data_0f28 COMMAND func_lockstep --states 5000
      --only ASOUND.117:887)
    add_test(NAME matched_asound_117_command_088e_data_0f34 COMMAND func_lockstep --states 5000
      --only ASOUND.117:88E)
    add_test(NAME matched_asound_117_command_0895_data_0f94 COMMAND func_lockstep --states 5000
      --only ASOUND.117:895)
    add_test(NAME matched_asound_117_command_08b7_data_0f52 COMMAND func_lockstep --states 5000
      --only ASOUND.117:8B7)
    add_test(NAME matched_asound_117_command_08d3_data_0f80 COMMAND func_lockstep --states 5000
      --only ASOUND.117:8D3)
    add_test(NAME matched_asound_117_command_08da_data_0f8a COMMAND func_lockstep --states 5000
      --only ASOUND.117:8DA)
    add_test(NAME matched_asound_117_command_08fc_data_0fb4 COMMAND func_lockstep --states 5000
      --only ASOUND.117:8FC)
    add_test(NAME matched_asound_117_speaker_timer_enable COMMAND func_lockstep --states 5000
      --only ASOUND.117:B74)
    add_test(NAME matched_asound_117_speaker_timer_return COMMAND func_lockstep --states 5000
      --only ASOUND.117:B75)
    add_test(NAME matched_asound_log_sample_pit_delta COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:DA)
    add_test(NAME matched_asound_log_finish_pit_delta COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:FA)
    add_test(NAME matched_asound_log_install_timer_reload COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:423)
    add_test(NAME matched_asound_log_install_timer_enable COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:42C)
    add_test(NAME matched_rsound_117_mpu_send_command_wait_ack COMMAND func_lockstep --states 5000
      --only RSOUND.117:C43)
    add_test(NAME matched_rsound_117_mpu_write_data_wait_ready COMMAND func_lockstep --states 5000
      --only RSOUND.117:C7C)
    add_test(NAME matched_rsound_log_mpu_send_command_wait_ack COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:2D4)
    add_test(NAME matched_rsound_log_mpu_write_data_wait_ready COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:310)
    add_test(NAME matched_asound_log_opl_register_write COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:300)
    add_test(NAME matched_asound_log_opl_register_write_args COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:2F6)
    add_test(NAME matched_isound_117_select_effect_record_025d COMMAND func_lockstep --states 5000
      --only ISOUND.117:62E)
    add_test(NAME matched_isound_117_decode_word_effect_record COMMAND func_lockstep --states 5000
      --only ISOUND.117:443)
    add_test(NAME matched_asound_log_load_instrument_operators COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:BFC)
    add_test(NAME matched_asound_117_emit_operator_registers COMMAND func_lockstep --states 5000
      --only ASOUND.117:1048)
    add_test(NAME matched_asound_117_load_instrument_pair COMMAND func_lockstep --states 5000
      --only ASOUND.117:123E)
    add_test(NAME matched_asound_117_switch_voice_slot COMMAND func_lockstep --states 5000
      --only ASOUND.117:E8A)
    add_test(NAME matched_rsound_117_voice_step COMMAND func_lockstep --states 5000
      --only RSOUND.117:2)
    add_test(NAME matched_asound_117_reset_voice_records_with_data_offsets COMMAND func_lockstep --states 5000
      --only ASOUND.117:720)
    add_test(NAME matched_asound_log_detect_adlib_and_init COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:140)
    add_test(NAME matched_rsound_117_step_eight_voices COMMAND func_lockstep --states 5000
      --only RSOUND.117:50A)
    add_test(NAME matched_asound_117_reset_voice_records_with_data_offsets_0694 COMMAND func_lockstep --states 5000
      --only ASOUND.117:694)
    add_test(NAME matched_asound_117_run_voice_records_1882 COMMAND func_lockstep --states 5000
      --only ASOUND.117:1882)
    add_test(NAME matched_isound_log_shutdown_entry COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:A2)
    add_test(NAME matched_rsound_log_send_message_with_checksum COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:349)
    add_test(NAME matched_asound_117_opl_noise_step_0562 COMMAND func_lockstep --states 5000
      --only ASOUND.117:562)
    add_test(NAME matched_rsound_117_send_pitch_bend_center COMMAND func_lockstep --states 5000
      --only RSOUND.117:E4C)
    add_test(NAME matched_asound_log_run_voice_records_116e COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:116E)
    add_test(NAME matched_rsound_117_send_message_with_checksum COMMAND func_lockstep --states 5000
      --only RSOUND.117:CAE)
    add_test(NAME matched_asound_117_noise_step_far_entry COMMAND func_lockstep --states 5000
      --only ASOUND.117:C1C)
    add_test(NAME matched_rsound_log_send_message_list COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:390)
    add_test(NAME matched_rsound_117_send_message_list COMMAND func_lockstep --states 5000
      --only RSOUND.117:CF5)
    add_test(NAME matched_rsound_log_add_to_checksum COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:306)
    add_test(NAME matched_rsound_117_add_to_checksum COMMAND func_lockstep --states 5000
      --only RSOUND.117:C72)
    add_test(NAME matched_rsound_117_send_channel_volume_100 COMMAND func_lockstep --states 5000
      --only RSOUND.117:E70)
    add_test(NAME matched_rsound_117_all_notes_off_and_reset_channels COMMAND func_lockstep --states 5000
      --only RSOUND.117:112F)
    add_test(NAME matched_rsound_117_init_channels_once COMMAND func_lockstep --states 5000
      --only RSOUND.117:C2D)
    add_test(NAME matched_rsound_log_send_byte_far_entry COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:3FA)
    add_test(NAME matched_isound_117_shutdown_entry COMMAND func_lockstep --states 5000
      --only ISOUND.117:79)
    add_test(NAME matched_isound_117_set_dos_time_from_bios_clock COMMAND func_lockstep --states 5000
      --only ISOUND.117:45)
    add_test(NAME matched_asound_117_set_dos_time_from_bios_clock COMMAND func_lockstep --states 5000
      --only ASOUND.117:BD4)
    add_test(NAME matched_asound_117_noise_voice_status_far_entry COMMAND func_lockstep --states 5000
      --only ASOUND.117:C08)
    add_test(NAME matched_asound_117_set_voice_mode_far_entry COMMAND func_lockstep --states 5000
      --only ASOUND.117:B76)
    add_test(NAME matched_asound_117_sound_blaster_dsp_reset COMMAND func_lockstep --states 5000
      --only ASOUND.117:197B)
    add_test(NAME matched_asound_117_reset_voice_records COMMAND func_lockstep --states 5000
      --only ASOUND.117:18F)
    add_test(NAME matched_asound_117_silence_opl_chip COMMAND func_lockstep --states 5000
      --only ASOUND.117:210)
    add_test(NAME matched_asound_117_start_song_0ec0_random_variant COMMAND func_lockstep --states 5000
      --only ASOUND.117:64D)
    add_test(NAME matched_asound_117_start_song_0eda_random_variant COMMAND func_lockstep --states 5000
      --only ASOUND.117:670)
    add_test(NAME matched_asound_117_command_0876_data_by_bx_bits_3_4 COMMAND func_lockstep --states 5000
      --only ASOUND.117:876)
    add_test(NAME matched_asound_117_set_rate_one_if_zero_then_data_0fa2_08e8 COMMAND func_lockstep --states 5000
      --only ASOUND.117:8E8)
    add_test(NAME matched_asound_117_command_089c_data_0f3c_unless_playing_1964 COMMAND func_lockstep --states 5000
      --only ASOUND.117:89C)
    add_test(NAME matched_asound_117_command_085f_data_by_bx_bits_2_3 COMMAND func_lockstep --states 5000
      --only ASOUND.117:85F)
    add_test(NAME matched_asound_117_reset_voice_records_two_with_data_offsets_0903 COMMAND func_lockstep --states 5000
      --only ASOUND.117:903)
    add_test(NAME matched_asound_117_command_0640_data_0ea4 COMMAND func_lockstep --states 5000
      --only ASOUND.117:640)
    add_test(NAME matched_asound_117_command_0662_data_0ed2 COMMAND func_lockstep --states 5000
      --only ASOUND.117:662)
    add_test(NAME matched_asound_117_command_0669_data_0eca COMMAND func_lockstep --states 5000
      --only ASOUND.117:669)
    add_test(NAME matched_asound_117_command_068d_data_0eec COMMAND func_lockstep --states 5000
      --only ASOUND.117:68D)
    add_test(NAME matched_asound_117_reset_voice_records_with_data_offsets_06be COMMAND func_lockstep --states 5000
      --only ASOUND.117:6BE)
    add_test(NAME matched_asound_117_reset_voice_records_four_with_data_offsets_06e8 COMMAND func_lockstep --states 5000
      --only ASOUND.117:6E8)
    add_test(NAME matched_asound_117_reset_voice_records_four_with_data_offsets_0704 COMMAND func_lockstep --states 5000
      --only ASOUND.117:704)
    add_test(NAME matched_asound_117_command_08be_data_0f5a COMMAND func_lockstep --states 5000
      --only ASOUND.117:8BE)
    add_test(NAME matched_asound_117_command_08cc_data_0f70 COMMAND func_lockstep --states 5000
      --only ASOUND.117:8CC)
    add_test(NAME matched_asound_117_command_08e1_data_0f78 COMMAND func_lockstep --states 5000
      --only ASOUND.117:8E1)
    add_test(NAME matched_rsound_117_dispatch_command COMMAND func_lockstep --states 5000
      --only RSOUND.117:1225)
    add_test(NAME matched_asound_117_chip_command_write COMMAND func_lockstep --states 5000
      --only ASOUND.117:1BCC)
    add_test(NAME matched_asound_117_chip_detect_sequence COMMAND func_lockstep --states 5000
      --only ASOUND.117:1BAF)
    add_test(NAME matched_isound_117_store_volume_table_args COMMAND func_lockstep --states 5000
      --only ISOUND.117:23)
    add_test(NAME matched_isound_117_volume_scaler_far_entry COMMAND func_lockstep --states 5000
      --only ISOUND.117:7CF)
    add_test(NAME matched_end_format_dispatch COMMAND func_lockstep --states 5000
      --only END.EXE:57B0)
    add_test(NAME matched_isound_log_dispatch_command COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:B8)
    add_test(NAME matched_asound_log_start_channel_note COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:804)
    add_test(NAME matched_asound_log_channel_tick COMMAND func_lockstep --states 5000
      --only ASOUND.LOG:D1A)
    add_test(NAME matched_asound_117_voice_script_step_1360 COMMAND func_lockstep --states 5000
      --only ASOUND.117:1360)
    add_test(NAME matched_mps_logo_huge_memcpy COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:822)
    add_test(NAME matched_mps_logo_strstr COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:67C)
    add_test(NAME matched_start_divide_error_handler COMMAND func_lockstep --states 5000
      --only START.EXE:8F94)
    add_test(NAME matched_rsound_117_start_voices_one_to_four_then_nine COMMAND func_lockstep --states 5000
      --only RSOUND.117:F98)
    add_test(NAME matched_rsound_log_channel_tick COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:8BE)
    add_test(NAME matched_vgame_close_picture_handle COMMAND func_lockstep --states 5000
      --only VGAME.EXE:D569)
    add_test(NAME matched_isound_log_start_note_at_ac COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:675)
    add_test(NAME matched_isound_log_start_speaker_note COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:468)
    add_test(NAME matched_player_retrace_resync_enter COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:211)
    add_test(NAME matched_asound_117_adlib_tick COMMAND func_lockstep --states 5000
      --only ASOUND.117:430)
    add_test(NAME matched_vgame_perspective_divide_fault COMMAND func_lockstep --states 5000
      --only VGAME.EXE:971)
    add_test(NAME matched_dswap_string_to_long_jump COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:FF8)
    add_test(NAME matched_dswap_malloc_jump COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:2060)
    add_test(NAME matched_setup_malloc_jump COMMAND func_lockstep --states 5000
      --only SETUP.EXE:1C46)
    add_test(NAME matched_player_malloc_jump COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:222A)
    add_test(NAME matched_start_objective_kind_jump COMMAND func_lockstep --states 5000
      --only START.EXE:2846)
    add_test(NAME matched_end_debrief_message_switch COMMAND func_lockstep --states 5000
      --only END.EXE:36BC)
    add_test(NAME matched_player_clock_isr_enter COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1A3)
    add_test(NAME matched_asound_117_opl_voice_setup_prefix COMMAND func_lockstep --states 5000
      --only ASOUND.117:25A)
    add_test(NAME matched_player_exit_entry COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1255)
    add_test(NAME matched_mps_logo_sound_overlay_entry_100_jump COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:307)
    add_test(NAME matched_asound_117_dispatch_sequencer_command COMMAND func_lockstep --states 5000
      --only ASOUND.117:B98)
    add_test(NAME matched_player_set_video_mode COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:540)
    add_test(NAME matched_rsound_117_tick_on_private_stack COMMAND func_lockstep --states 5000
      --only RSOUND.117:BAD)
    add_test(NAME matched_rsound_117_sample_pit_interval COMMAND func_lockstep --states 5000
      --only RSOUND.117:ACE)
    add_test(NAME matched_rsound_log_init_four_voice_records COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:48E)
    add_test(NAME matched_rsound_log_read_pit_interval COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:FD)
    add_test(NAME matched_isound_log_set_dos_time_from_bios_clock COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:82)
    add_test(NAME matched_asound_117_shutdown_far_entry COMMAND func_lockstep --states 5000
      --only ASOUND.117:BB5)
    add_test(NAME matched_isound_117_effect_duration_step COMMAND func_lockstep --states 5000
      --only ISOUND.117:200)
    add_test(NAME matched_asound_117_opl_key_on_frequency_030b COMMAND func_lockstep --states 5000
      --only ASOUND.117:30B)
    add_test(NAME matched_rsound_117_measure_delay_passes COMMAND func_lockstep --states 5000
      --only RSOUND.117:AFE)
    add_test(NAME matched_asound_117_write_levels_50_53 COMMAND func_lockstep --states 5000
      --only ASOUND.117:3FB)
    add_test(NAME matched_rsound_log_calibrate_message_delay COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:12D)
    add_test(NAME matched_asound_117_opl_write_register COMMAND func_lockstep --states 5000
      --only ASOUND.117:36B)
    add_test(NAME matched_dswap_patched_far_jump_thunk COMMAND func_lockstep --states 5000
      --only DSWAP.EXE:8DC,DSWAP.EXE:922,DSWAP.EXE:9AE,DSWAP.EXE:9DB,DSWAP.EXE:9E5,DSWAP.EXE:9F4,DSWAP.EXE:9FE,DSWAP.EXE:A03,DSWAP.EXE:A35,DSWAP.EXE:A3A,DSWAP.EXE:A53,DSWAP.EXE:A9E)
    add_test(NAME matched_end_patched_far_jump_thunk COMMAND func_lockstep --states 5000
      --only END.EXE:23FA,END.EXE:2413,END.EXE:243B,END.EXE:2440,END.EXE:2445,END.EXE:244A,END.EXE:2468,END.EXE:249A,END.EXE:24C2,END.EXE:24CC,END.EXE:24E5,END.EXE:24F9,END.EXE:2503,END.EXE:250D,END.EXE:2512,END.EXE:251C,END.EXE:2521,END.EXE:2553,END.EXE:2558,END.EXE:2571,END.EXE:2576,END.EXE:257B,END.EXE:2580,END.EXE:25BC,END.EXE:25C1,END.EXE:25D0,END.EXE:25F3,END.EXE:25FD,END.EXE:2602)
    add_test(NAME matched_mps_logo_sound_overlay_entry_030e_jump COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:30E)
    add_test(NAME matched_mps_logo_sound_overlay_entry_0315_jump COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:315)
    add_test(NAME matched_asound_117_opl_write_register_stack_args COMMAND func_lockstep --states 5000
      --only ASOUND.117:35C)
    add_test(NAME matched_asound_117_driver_command_dispatch COMMAND func_lockstep --states 5000
      --only ASOUND.117:18C8)
    add_test(NAME matched_asound_117_sound_effect_restore COMMAND func_lockstep --states 5000
      --only ASOUND.117:A33)
    add_test(NAME matched_start_format_signed_decimal_setup COMMAND func_lockstep --states 5000
      --only START.EXE:9FD6)
    add_test(NAME matched_setup_joystick_read_finish COMMAND func_lockstep --states 5000
      --only SETUP.EXE:94)
    add_test(NAME matched_player_format_exit COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:1F4D)
    add_test(NAME matched_vgame_program_main COMMAND func_lockstep --states 5000
      --only VGAME.EXE:10)
    add_test(NAME matched_start_format_plain_char_out COMMAND func_lockstep --states 5000
      --only START.EXE:9D5C)
    add_test(NAME matched_start_patched_far_jump_thunk COMMAND func_lockstep --states 5000
      --only START.EXE:69B8,START.EXE:69D1,START.EXE:69F9,START.EXE:69FE,START.EXE:6A03,START.EXE:6A08,START.EXE:6A26,START.EXE:6A58,START.EXE:6A80,START.EXE:6A8A,START.EXE:6AA3,START.EXE:6AB7,START.EXE:6ACB,START.EXE:6ADA,START.EXE:6ADF,START.EXE:6AE4,START.EXE:6B11,START.EXE:6B16,START.EXE:6B2F,START.EXE:6B34,START.EXE:6B39,START.EXE:6B3E,START.EXE:6B7A,START.EXE:6B7F,START.EXE:6B8E,START.EXE:6BB1,START.EXE:6BBB,START.EXE:6BC0)
    add_test(NAME matched_player_patched_far_jump_thunk COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:13B9,PLAYER.EXE:13C3,PLAYER.EXE:13C8)
    add_test(NAME matched_setup_patched_far_jump_thunk COMMAND func_lockstep --states 5000
      --only SETUP.EXE:A72,SETUP.EXE:A77)
    add_test(NAME matched_asound_117_detect_sound_card COMMAND func_lockstep --states 5000
      --only ASOUND.117:5E)
    add_test(NAME matched_asound_117_pit_counter2_sync_start COMMAND func_lockstep --states 5000
      --only ASOUND.117:A17)
    add_test(NAME matched_rsound_117_mpu_reset_wait_ack COMMAND func_lockstep --states 5000
      --only RSOUND.117:D07)
    add_test(NAME matched_rsound_log_mpu_reset_wait_ack COMMAND func_lockstep --states 5000
      --only RSOUND.LOG:3A2)
    add_test(NAME matched_mps_logo_format_conversion_init COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:1046)
    add_test(NAME matched_mps_logo_format_literal_char COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:103B)
    add_test(NAME matched_mps_logo_format_dispatch COMMAND func_lockstep --states 5000
      --only MPS_LOGO.EXE:111D)
    add_test(NAME matched_isound_log_enable_interrupts_after_install COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:60C)
    add_test(NAME matched_isound_log_reload_timer_slow_rate COMMAND func_lockstep --states 5000
      --only ISOUND.LOG:5D1)
    add_test(NAME matched_start_return_after_timer_reload_sti COMMAND func_lockstep --states 5000
      --only START.EXE:8DD7)
    add_test(NAME matched_end_return_after_timer_reload_sti COMMAND func_lockstep --states 5000
      --only END.EXE:4CBB)
    add_test(NAME matched_player_return_after_timer_calibrate COMMAND func_lockstep --states 5000
      --only PLAYER.EXE:306)
    add_test(NAME matched_vgame_far_atan2 COMMAND func_lockstep --states 5000
      --only VGAME.EXE:63)
    add_test(NAME matched_vgame_far_sine_scale COMMAND func_lockstep --states 5000
      --only VGAME.EXE:DB)
    add_test(NAME matched_vgame_far_cosine_scale COMMAND func_lockstep --states 5000
      --only VGAME.EXE:11C)
    add_test(NAME matched_vgame_far_odd_table COMMAND func_lockstep --states 5000
      --only VGAME.EXE:160)
    add_test(NAME matched_vgame_matrix_attitude_angles COMMAND func_lockstep --states 5000
      --only VGAME.EXE:4D9)
    add_test(NAME matched_setup_random_next COMMAND func_lockstep --states 5000
      --only SETUP.EXE:17B0)
    add_test(NAME matched_setup_signed_divide COMMAND func_lockstep --states 5000
      --only SETUP.EXE:17D6)
    add_test(NAME matched_setup_ulong_multiply COMMAND func_lockstep --states 5000
      --only SETUP.EXE:1870)
    add_test(NAME matched_setup_signed_remainder COMMAND func_lockstep --states 5000
      --only SETUP.EXE:18A2)
    add_test(NAME matched_setup_shift_right_long COMMAND func_lockstep --states 5000
      --only SETUP.EXE:1942)
    add_test(NAME matched_flight_plan COMMAND func_lockstep --states 4000
      --only START.EXE:149C)
    add_test(NAME matched_picture_input COMMAND func_lockstep --states 4000
      --only PLAYER.EXE:B84,PLAYER.EXE:BA2,PLAYER.EXE:5F6)
    add_test(NAME matched_timer_restore COMMAND func_lockstep --states 4000
      --only START.EXE:8CE6,END.EXE:4BCA,SETUP.EXE:FFE,PLAYER.EXE:4D)
    add_test(NAME matched_mouse_wrappers COMMAND func_lockstep --states 4000
      --only PLAYER.EXE:F47,END.EXE:44B9,END.EXE:44E7,START.EXE:8545,START.EXE:8573,SETUP.EXE:4)
    add_test(NAME matched_setup_services COMMAND func_lockstep --states 4000
      --only SETUP.EXE:C84,SETUP.EXE:14BE,SETUP.EXE:1387,SETUP.EXE:BFB)
    add_test(NAME matched_record_tables COMMAND func_lockstep --states 4000
      --only MPS_LOGO.EXE:491,END.EXE:45AD)
    add_test(NAME matched_file_heap_services COMMAND func_lockstep --states 4000
      --only MPS_LOGO.EXE:4C4,MPS_LOGO.EXE:43D,START.EXE:A8C2)
    add_test(NAME matched_timer_tick COMMAND func_lockstep --states 4000
      --only VGAME.EXE:1E79)
    add_test(NAME matched_span_emit COMMAND func_lockstep --states 4000
      --only VGAME.EXE:5A4,VGAME.EXE:5B3)
    add_test(NAME matched_span_edge COMMAND func_lockstep --states 4000
      --only VGAME.EXE:6A6)
    add_test(NAME matched_polygon_fill COMMAND func_lockstep --states 4000
      --only VGAME.EXE:1E6)
  endif()
endif()
