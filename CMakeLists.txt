cmake_minimum_required(VERSION 3.25)

project(DemiEngine
  VERSION 0.1.0
  DESCRIPTION "AI-friendly C++/Lua game engine scaffold for Linux 2D and isometric 2.5D games"
  LANGUAGES CXX
)

include(FetchContent)

FetchContent_Declare(sol2
  GIT_REPOSITORY https://github.com/ThePhD/sol2.git
  GIT_TAG c1f95a773c6f8f4fde8ca3efe872e7286afe4444
)
FetchContent_MakeAvailable(sol2)

set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_USER_SETTINGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(box2d
  GIT_REPOSITORY https://github.com/erincatto/box2d.git
  GIT_TAG v2.4.1
)
FetchContent_MakeAvailable(box2d)

FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.22
)
FetchContent_MakeAvailable(miniaudio)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RPRAND ON CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
)
FetchContent_MakeAvailable(raylib)

option(DEMI_ENABLE_NETWORK "Enable optional ENet networking module" OFF)
option(DEMI_ENABLE_MEDIA "Enable FFmpeg-backed media module" ON)

if(DEMI_ENABLE_NETWORK)
  FetchContent_Declare(enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
  )
  FetchContent_MakeAvailable(enet)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(demi-core STATIC
  src/demi/assets/AssetRegistry.cpp
  src/demi/diagnostics/Diagnostic.cpp
  src/demi/filesystem/ProjectPaths.cpp
  src/demi/schema/Validation.cpp
)

target_include_directories(demi-core PUBLIC src)

target_compile_features(demi-core PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(demi-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
  pkg_check_modules(LUA54 QUIET IMPORTED_TARGET lua5.4)
  if(DEMI_ENABLE_MEDIA)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
  endif()
endif()

if(DEMI_ENABLE_MEDIA AND NOT TARGET PkgConfig::FFMPEG)
  message(FATAL_ERROR "DEMI_ENABLE_MEDIA requires FFmpeg development packages: libavformat, libavcodec, libavutil, and libswscale.")
endif()

add_library(demi-runtime-lib STATIC
  src/demi/runtime/app/RuntimeApp.cpp
  src/demi/runtime/audio/AudioClipPreprocess.cpp
  src/demi/runtime/audio/AudioSystem.cpp
  src/demi/runtime/audio/MiniaudioAudioBackend.cpp
  src/demi/runtime/media/MediaSystem.cpp
  src/demi/runtime/network/NetworkSystem.cpp
  src/demi/runtime/physics/Physics2D.cpp
  src/demi/runtime/physics/Physics3D.cpp
  src/demi/runtime/render/Renderer2D.cpp
  src/demi/runtime/render/Renderer3D.cpp
  src/demi/runtime/render/VoxelRenderer.cpp
  src/demi/runtime/scene/HudParser.cpp
  src/demi/runtime/scene/ProjectParser.cpp
  src/demi/runtime/scene/VoxelChunkBuilder.cpp
  src/demi/runtime/scene/SceneEntityParser.cpp
  src/demi/runtime/scene/SceneJson.cpp
  src/demi/runtime/scene/SceneLoader.cpp
  src/demi/runtime/scripting/LuaScriptHostBindings.cpp
  src/demi/runtime/scripting/LuaScriptHost.cpp
  src/demi/runtime/scripting/LuaBindingCleanup.cpp
  src/demi/runtime/scripting/LuaScriptAnnotations.cpp
  src/demi/runtime/scripting/LuaScriptDiagnostics.cpp
  src/demi/runtime/scripting/LuaScriptHostEntityServices.cpp
  src/demi/runtime/scripting/LuaScriptHostHudServices.cpp
  src/demi/runtime/scripting/LuaScriptHostLoading.cpp
  src/demi/runtime/scripting/LuaScriptHostMediaNetworkServices.cpp
  src/demi/runtime/scripting/LuaScriptProperties.cpp
  src/demi/runtime/scripting/bindings/LuaBindingHelpers.cpp
  src/demi/runtime/scripting/bindings/LuaCoreBindings.cpp
  src/demi/runtime/scripting/bindings/LuaEntityBindings.cpp
  src/demi/runtime/scripting/bindings/LuaHudMediaBindings.cpp
  src/demi/runtime/scripting/bindings/LuaJsonBridge.cpp
  src/demi/runtime/scripting/bindings/LuaNetworkBindings.cpp
  src/demi/runtime/scripting/bindings/LuaNetworkSessionBindings.cpp
  src/demi/runtime/scripting/persistence/LuaSaveCodec.cpp
  src/demi/runtime/scripting/LuaScriptHostPersistence.cpp
  src/demi/runtime/scripting/LuaScriptHostServices.cpp
  src/demi/runtime/voxel/VoxelBlockSet.cpp
  src/demi/runtime/voxel/VoxelChunk.cpp
  src/demi/runtime/voxel/VoxelMesher.cpp
  src/demi/runtime/voxel/VoxelTerrain.cpp
)

target_link_libraries(demi-runtime-lib PUBLIC demi-core)
target_include_directories(demi-runtime-lib PUBLIC src)
target_compile_features(demi-runtime-lib PUBLIC cxx_std_20)
target_link_libraries(demi-runtime-lib PRIVATE box2d miniaudio nlohmann_json::nlohmann_json)
target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_BOX2D=1 DEMI_HAS_MINIAUDIO=1 DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(demi-runtime-lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(demi-runtime-lib PRIVATE raylib)
target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_RAYLIB=1)

if(TARGET PkgConfig::LUA54)
  target_link_libraries(demi-runtime-lib PRIVATE PkgConfig::LUA54)
else()
  message(FATAL_ERROR "Lua 5.4 was not found. Install lua5.4 development files to build DemiEngine scripting.")
endif()

target_link_libraries(demi-runtime-lib PRIVATE sol2::sol2)
target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_SOL2=1)

if(DEMI_ENABLE_MEDIA)
  target_link_libraries(demi-runtime-lib PRIVATE PkgConfig::FFMPEG)
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_FFMPEG=1)
else()
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_FFMPEG=0)
endif()

if(DEMI_ENABLE_NETWORK)
  target_link_libraries(demi-runtime-lib PRIVATE enet)
  target_include_directories(demi-runtime-lib PRIVATE ${enet_SOURCE_DIR}/include)
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_ENET=1)
else()
  target_compile_definitions(demi-runtime-lib PRIVATE DEMI_HAS_ENET=0)
endif()

add_executable(demi src/cli/main.cpp)
target_link_libraries(demi PRIVATE demi-core demi-runtime-lib)
target_compile_definitions(demi PRIVATE DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_executable(demi-runtime src/runtime/main.cpp)
target_link_libraries(demi-runtime PRIVATE demi-core demi-runtime-lib)

add_executable(demi-editor src/editor/main.cpp)
target_link_libraries(demi-editor PRIVATE demi-core)

add_executable(demi-smoke-tests tests/smoke_tests.cpp)
target_link_libraries(demi-smoke-tests PRIVATE demi-core)

add_executable(demi-physics2d-tests tests/physics2d_tests.cpp)
target_link_libraries(demi-physics2d-tests PRIVATE demi-runtime-lib)

add_executable(demi-physics3d-tests tests/physics3d_tests.cpp)
target_link_libraries(demi-physics3d-tests PRIVATE demi-runtime-lib)

add_executable(demi-lua-stub-contract-tests tests/lua_stub_contract_tests.cpp)
target_link_libraries(demi-lua-stub-contract-tests PRIVATE demi-runtime-lib)

add_executable(demi-lua-scripting-tests tests/lua_scripting_tests.cpp)
target_link_libraries(demi-lua-scripting-tests PRIVATE demi-runtime-lib)

add_executable(demi-scene-loader-tests tests/scene_loader_tests.cpp)
target_link_libraries(demi-scene-loader-tests PRIVATE demi-runtime-lib)

add_executable(demi-voxel-tests tests/voxel_tests.cpp)
target_link_libraries(demi-voxel-tests PRIVATE demi-runtime-lib)

add_executable(demi-snapshot-replication-tests tests/snapshot_replication_tests.cpp)
target_link_libraries(demi-snapshot-replication-tests PRIVATE demi-runtime-lib)

if(DEMI_ENABLE_NETWORK)
  add_executable(demi-network-tests tests/network_tests.cpp)
  target_link_libraries(demi-network-tests PRIVATE demi-runtime-lib)
endif()

enable_testing()
add_test(NAME demi-smoke-tests COMMAND demi-smoke-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-physics2d-tests COMMAND demi-physics2d-tests)
add_test(NAME demi-physics3d-tests COMMAND demi-physics3d-tests)
add_test(NAME demi-lua-stub-contract-tests COMMAND demi-lua-stub-contract-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-lua-scripting-tests COMMAND demi-lua-scripting-tests)
add_test(NAME demi-scene-loader-tests COMMAND demi-scene-loader-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-voxel-tests COMMAND demi-voxel-tests)
add_test(NAME demi-snapshot-replication-tests COMMAND demi-snapshot-replication-tests)
if(DEMI_ENABLE_NETWORK)
  add_test(NAME demi-network-tests COMMAND demi-network-tests)
endif()
add_test(NAME demi-runtime-minimal-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/demi.project.json --frames 1
)
set_tests_properties(demi-runtime-minimal-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-networking-minimal-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/demi.project.json --frames 1
)
set_tests_properties(demi-runtime-networking-minimal-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-minimal-3d-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_3d/demi.project.json --frames 1
)
set_tests_properties(demi-runtime-minimal-3d-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-minimal-voxel-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/demi.project.json --frames 1
)
set_tests_properties(demi-runtime-minimal-voxel-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-validate-minimal-2d-networking
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/demi.project.json
)
add_test(NAME demi-validate-minimal-3d
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_3d/demi.project.json
)
add_test(NAME demi-validate-minimal-voxel
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/demi.project.json
)
add_test(NAME demi-script-check-minimal-player
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/scripts/player.lua
)
add_test(NAME demi-script-check-minimal-voxel-fly-camera
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/scripts/fly_camera.lua
)
add_test(NAME demi-script-check-minimal-voxel-worldgen
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/scripts/worldgen.lua
)
add_test(NAME demi-script-check-runtime-net-snapshot-replication
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/scripts/runtime/demi/net/snapshot_replication.lua
)
set(DEMI_NETWORKING_EXAMPLE_SCRIPTS
  game
  game/collectibles
  game/hud
  game/score
  game/world
  game_state
  levels/platformer
  levels/spiral
  main_menu
  menu/actions
  menu/network
  menu/settings
  menu/view
  menu_scene
  network_replication
  player
  player_config
  player_platformer
  player_slingshot
)
foreach(script_name IN LISTS DEMI_NETWORKING_EXAMPLE_SCRIPTS)
  add_test(NAME demi-script-check-networking-${script_name}
    COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/scripts/${script_name}.lua
  )
endforeach()
add_test(NAME demi-lua-stubs-generate
  COMMAND demi lua-stubs generate ${CMAKE_BINARY_DIR}/generated/demi.lua
)
