cmake_minimum_required(VERSION 3.20)
project(hlds-convert LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# A 32-bit Windows program may use 4 GB on a 64-bit Windows, not 2, if it says
# it can: maps are cooked several at once.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT MSVC)
    add_link_options(-Wl,--large-address-aware)
endif()

# ---------------------------------------------------------------------------
# meshoptimizer (thirdparty/meshoptimizer, MIT): the model meshes' thinning and
# strips. Only the parts of it the converter uses, as a library of its own --
# it is not ours to hold to our warnings. Floating-point contraction is off so
# that every machine thins a model to the same triangles.
# ---------------------------------------------------------------------------
add_library(meshoptimizer STATIC
    "${REPO_ROOT}/thirdparty/meshoptimizer/indexgenerator.cpp"
    "${REPO_ROOT}/thirdparty/meshoptimizer/rasterizer.cpp"
    "${REPO_ROOT}/thirdparty/meshoptimizer/simplifier.cpp"
    "${REPO_ROOT}/thirdparty/meshoptimizer/stripifier.cpp"
    "${REPO_ROOT}/thirdparty/meshoptimizer/vcacheoptimizer.cpp"
)
if(NOT MSVC)
    target_compile_options(meshoptimizer PRIVATE -ffp-contract=off)
endif()

# ---------------------------------------------------------------------------
# Core -- pure C++20, NO Qt.
#
# The GUI and the CLI are both thin drivers over this. Keeping Qt out is what
# lets the tests run fast and headless, and what lets a CI job without Qt still
# verify the conversion logic.
# ---------------------------------------------------------------------------
add_library(hlds_convert_core STATIC
    core/src/AtomicWrite.cpp
    core/src/BrushInfo.cpp
    core/src/BspFile.cpp
    core/src/Converter.cpp
    core/src/Cooker.cpp
    core/src/EntityCook.cpp
    core/src/EntityParser.cpp
    core/src/FaceBuilder.cpp
    core/src/GxStream.cpp
    core/src/LevelWriter.cpp
    core/src/LightBake.cpp
    core/src/Lz77.cpp
    core/src/MdlFile.cpp
    core/src/ModelCook.cpp
    core/src/ModelMesh.cpp
    core/src/NodGraph.cpp
    core/src/NpacWriter.cpp
    core/src/Pack.cpp
    core/src/MapStats.cpp
    core/src/Materials.cpp
    core/src/MeshOptimize.cpp
    core/src/PathNodes.cpp
    core/src/Portals.cpp
    core/src/SoundCook.cpp
    core/src/SpriteCook.cpp
    core/src/TextureEncoder.cpp
    core/src/TitlesFile.cpp
    core/src/AviFile.cpp
    core/src/UiCook.cpp
    core/src/TextureBudget.cpp
    core/src/TextureFamily.cpp
    core/src/TextureCook.cpp
    core/src/TextureImage.cpp
    core/src/TextureSize.cpp
    core/src/WadFile.cpp
)

# The version, from `git describe`: the same one the ROM gets (tools/buildid.py).
# Run at every build; the file only changes when the version does.
set(HLDS_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.gen.cpp")
add_custom_target(hlds_version
    COMMAND ${CMAKE_COMMAND} -DROOT=${REPO_ROOT} -DOUT=${HLDS_VERSION_FILE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.cmake
    BYPRODUCTS ${HLDS_VERSION_FILE})
target_sources(hlds_convert_core PRIVATE ${HLDS_VERSION_FILE})
add_dependencies(hlds_convert_core hlds_version)

target_include_directories(hlds_convert_core PUBLIC
    core/include
    "${REPO_ROOT}/shared"      # the formats the DS also compiles
    "${REPO_ROOT}/thirdparty"  # <yas/...>, same spelling as the ARM9 build
)

find_package(Threads REQUIRED)
target_link_libraries(hlds_convert_core PUBLIC Threads::Threads PRIVATE meshoptimizer)

# yas reads its byte order from these, exactly as the ARM9 build pins it
# (build.py): both sides must be told the same thing, explicitly.
target_compile_definitions(hlds_convert_core PUBLIC _LITTLE_ENDIAN)

if(NOT MSVC)
    target_compile_options(hlds_convert_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(hlds-convert cli/main.cpp)
target_link_libraries(hlds-convert PRIVATE hlds_convert_core)

# ---------------------------------------------------------------------------
# Tests
#
# No Valve data is in this repository and none may be added, so every test
# here runs on a level built in memory (tests/TestBspBuilder.hpp). Tests that
# want real maps read HLDS_VALVE_DIR from the environment and leave those checks
# out without it; one that then has nothing left to check returns 77: skipped.
# ---------------------------------------------------------------------------
# The installer's decisions: no Qt either, and tested with the rest.
add_library(hlds_install STATIC install/Install.cpp)
target_include_directories(hlds_install PUBLIC install)
target_compile_features(hlds_install PUBLIC cxx_std_20)

option(HLDS_BUILD_TESTS "Build the converter tests" ON)
if(HLDS_BUILD_TESTS)
    enable_testing()
    # The suites are built unoptimised: the game is header-only (shared/), so
    # each suite compiles all of it again, and optimising that took five times
    # as long (test_monsters: 220 s against 40 s) for runs that are short
    # either way. The converter itself stays optimised.
    set(HLDS_TEST_OPTIMISE $<IF:$<CXX_COMPILER_ID:MSVC>,/Od,-O0>)
    add_executable(test_install tests/test_install.cpp)
    target_compile_options(test_install PRIVATE ${HLDS_TEST_OPTIMISE})
    target_link_libraries(test_install PRIVATE hlds_install)
    add_test(NAME test_install COMMAND test_install)
    foreach(test_name
            test_anim
            test_brush
            test_bsp
            test_cook
            test_entities
            test_geometry
            test_level
            test_logic
            test_track
            test_monsters
            test_ai
            test_talk
            test_soldiers
            test_ui
            test_hud
            test_lod
            test_titles
            test_view
            test_drawplan
            test_controls
            test_commands
            test_lineedit
            test_avi
            test_net
            test_netmaps
            test_netnext
            test_demo
            test_airfetch
            test_people
            test_boxes
            test_families
            test_incident
            test_mixer
            test_roomdsp
            test_audiolink
            test_stereomusic
            test_dsproom
            test_cascade
            test_actors
            test_sound
            test_sprite
            test_effects
            test_manifest
            test_npac
            test_graph
            test_pack
            test_mdl
            test_mesh
            test_movers
            test_physics
            test_dsp
            test_save
            test_savefiles
            test_savestate
            test_texture
            test_texturestream
            test_wad
            test_aliens
            test_military
            test_tanks
            test_bosses
            test_xen
            test_portals
            test_gxbounds
            test_brushplan
            test_round39
            test_revisit
            test_skill
            test_achievements)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_compile_options(${test_name} PRIVATE ${HLDS_TEST_OPTIMISE})
        target_link_libraries(${test_name} PRIVATE hlds_convert_core)
        add_test(NAME ${test_name} COMMAND ${test_name})
        # (A suite with nothing to check without the game says so with this.)
        set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77)
    endforeach()

    # The DSi's DSP program (teak/, assembly) run in an emulated DSP and held
    # against its model, sample for sample (tools/dspsim). Skipped where the
    # emulator or the Teak toolchain is not there: the script says which.
    add_test(NAME dspsim COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/../dspsim/run.sh)
    set_tests_properties(dspsim PROPERTIES SKIP_RETURN_CODE 2 ENVIRONMENT "DSPSIM_BUDGET=25")

    # Not a test: writes a synthetic map with one brush of each kind in front
    # of the player, for looking at on the console (tests/make_test_map.cpp).
    add_executable(hlds-testmap tests/make_test_map.cpp)
    target_link_libraries(hlds-testmap PRIVATE hlds_convert_core)
endif()

# ---------------------------------------------------------------------------
# GUI -- Qt Widgets, optional.
#
# A thin driver over the core, for players who would rather pick two folders
# than type a command. Left out, with a note, when Qt is not installed: the
# CLI converts exactly the same files. Qt 6 where there is one; Qt 5.15 for
# the systems Qt 6 has left behind (32-bit Windows).
# ---------------------------------------------------------------------------
option(HLDS_BUILD_GUI "Build the Qt converter GUI" ON)
option(HLDS_QT5 "Build the GUI with Qt 5.15, not Qt 6" OFF)
if(HLDS_BUILD_GUI)
    if(NOT HLDS_QT5)
        find_package(Qt6 6.8 QUIET COMPONENTS Widgets)
    endif()
    if(Qt6_FOUND)
        set(HLDS_QT 6)
    else()
        find_package(Qt5 5.15 QUIET COMPONENTS Widgets)
        if(Qt5_FOUND)
            set(HLDS_QT 5)
        endif()
    endif()
    if(HLDS_QT)
        message(STATUS "hlds-convert-gui: Qt ${HLDS_QT}")
        add_subdirectory(gui)
    else()
        message(STATUS "Qt 6.8+ or 5.15 Widgets not found: hlds-convert-gui will not be built")
    endif()
endif()
