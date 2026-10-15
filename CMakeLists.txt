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
  src/present/drawlist.c src/present/drawfeed.c src/present/interp.c src/present/hires.c src/present/nearclip.c src/present/hud_state.c src/present/hud_cover.c src/present/hud_overlay.c src/present/band_state.c src/present/scene_mesh.c)
target_include_directories(f117present PUBLIC src/host src/present src/matched)
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
    set_target_properties(f117cpu f117machine f117present f117recomp_none
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
    add_executable(func_lockstep tests/func_lockstep.c)
    target_link_libraries(func_lockstep PRIVATE f117recomp)
    add_test(NAME func_lockstep COMMAND func_lockstep --states 2000)
    add_test(NAME matched_initializer_bounds COMMAND func_lockstep --states 4000
      --seed 0x66A860898896 --only SETUP.EXE:177C)
    # This seed aliases the shadow-text source with PUSH SS's stack write.
    add_test(NAME matched_shadow_stack_alias COMMAND func_lockstep --states 4000
      --seed 0xf2a7d2c1bf36 --only START.EXE:36C4,END.EXE:19C3,END.EXE:112A)
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
    add_test(NAME matched_projectile_stub_overwrite COMMAND func_lockstep --states 4000 --seed 0x1894deb34178
      --only VGAME.EXE:683E)
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
