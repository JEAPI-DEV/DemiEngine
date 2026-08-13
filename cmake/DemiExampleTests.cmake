# Example validation, headless runtime, packaging, replay, and script checks.
add_test(NAME demi-runtime-minimal-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/demi.project.json --max-frames 1
)
set_tests_properties(demi-runtime-minimal-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-networking-minimal-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/demi.project.json --max-frames 1
)
set_tests_properties(demi-runtime-networking-minimal-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-multiplayer-ffa-shooter-replay
  COMMAND demi run
    --project ${CMAKE_SOURCE_DIR}/examples/multiplayer_ffa_shooter/demi.project.json
    --input-replay ${CMAKE_SOURCE_DIR}/examples/multiplayer_ffa_shooter/replays/practice_match.replay.json
)
set_tests_properties(demi-runtime-multiplayer-ffa-shooter-replay PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-saves-simulation-debugging-replay
  COMMAND demi run
    --project ${CMAKE_SOURCE_DIR}/examples/saves_simulation_debugging/demi.project.json
    --input-replay ${CMAKE_SOURCE_DIR}/examples/saves_simulation_debugging/replays/move_and_roll.replay.json
    --profile-report ${CMAKE_BINARY_DIR}/generated/saves_simulation_debugging_profile.csv
)
set_tests_properties(demi-runtime-saves-simulation-debugging-replay PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-isometric-base-builder-replay
  COMMAND demi run
    --project ${CMAKE_SOURCE_DIR}/examples/isometric_base_builder/demi.project.json
    --input-replay ${CMAKE_SOURCE_DIR}/examples/isometric_base_builder/replays/build_and_defend.replay.json
)
set_tests_properties(demi-runtime-isometric-base-builder-replay PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-fighting-game-2d-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/fighting_game_2d/demi.project.json --max-frames 3
)
set_tests_properties(demi-runtime-fighting-game-2d-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-chess-frame
  COMMAND demi run
    --project ${CMAKE_SOURCE_DIR}/examples/chess/demi.project.json
    --max-frames 3
)
set_tests_properties(demi-runtime-chess-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-production-2d-foundation-frame
  COMMAND demi run
    --project ${CMAKE_SOURCE_DIR}/examples/production_2d_foundation/demi.project.json
    --max-frames 3
)
set_tests_properties(demi-runtime-production-2d-foundation-frame PROPERTIES
  ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-physics-2d-galton-board-replay
  COMMAND demi run
    --project ${CMAKE_SOURCE_DIR}/examples/physics_2d_galton_board/demi.project.json
    --input-replay
      ${CMAKE_SOURCE_DIR}/examples/physics_2d_galton_board/replays/release.replay.json
    --max-frames 360
)
set_tests_properties(demi-runtime-physics-2d-galton-board-replay PROPERTIES
  ENVIRONMENT "DEMI_HEADLESS=1"
  PASS_REGULAR_EXPRESSION "Galton board release complete: 60 balls")
add_test(NAME demi-cook-fighting-game-2d
  COMMAND demi cook
    --project ${CMAKE_SOURCE_DIR}/examples/fighting_game_2d/demi.project.json
    --platform linux
    --output ${CMAKE_BINARY_DIR}/generated/ctest-fighting-game-cooked
)
add_test(NAME demi-runtime-cooked-fighting-game-2d
  COMMAND demi run
    --project ${CMAKE_BINARY_DIR}/generated/ctest-fighting-game-cooked/demi.project.json
    --max-frames 3
)
set_tests_properties(demi-runtime-cooked-fighting-game-2d PROPERTIES
  DEPENDS demi-cook-fighting-game-2d
  ENVIRONMENT "DEMI_HEADLESS=1"
)
add_test(NAME demi-package-linux-fighting-game-2d
  COMMAND demi build linux
    --project ${CMAKE_SOURCE_DIR}/examples/fighting_game_2d/demi.project.json
    --output ${CMAKE_BINARY_DIR}/generated/ctest-fighting-game-linux
)
add_test(NAME demi-runtime-packaged-fighting-game-2d
  COMMAND ${CMAKE_BINARY_DIR}/generated/ctest-fighting-game-linux/fighting_game_2d
    --max-frames 3
)
set_tests_properties(demi-runtime-packaged-fighting-game-2d PROPERTIES
  DEPENDS demi-package-linux-fighting-game-2d
  ENVIRONMENT "DEMI_HEADLESS=1"
)
add_test(NAME demi-runtime-minimal-3d-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_3d/demi.project.json --max-frames 1
)
set_tests_properties(demi-runtime-minimal-3d-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-minimal-3d-profiler
  COMMAND demi run linux
    --project ${CMAKE_SOURCE_DIR}/examples/minimal_3d/demi.project.json
    --max-frames 3
    --profiler
)
set_tests_properties(demi-runtime-minimal-3d-profiler PROPERTIES
  ENVIRONMENT "DEMI_HEADLESS=1"
  PASS_REGULAR_EXPRESSION "DemiEngine runtime profile"
)
add_test(NAME demi-runtime-minimal-3d-gameplay-replay
  COMMAND demi run linux
    --project ${CMAKE_SOURCE_DIR}/examples/minimal_3d/demi.project.json
    --input-replay
      ${CMAKE_SOURCE_DIR}/examples/minimal_3d/replays/jump_and_fire.replay.json
    --profiler
)
set_tests_properties(demi-runtime-minimal-3d-gameplay-replay PROPERTIES
  ENVIRONMENT "DEMI_HEADLESS=1"
  PASS_REGULAR_EXPRESSION "Physics3D.create_shape,[^\n]*,7,0"
)
add_test(NAME demi-runtime-animation-3d-frame
  COMMAND demi run
    --project ${CMAKE_SOURCE_DIR}/examples/animation_3d/demi.project.json
    --max-frames 3
    --profile-report ${CMAKE_BINARY_DIR}/generated/animation_3d_profile.csv
)
set_tests_properties(demi-runtime-animation-3d-frame PROPERTIES
  ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-animation-3d-selection-replay
  COMMAND demi run
    --project ${CMAKE_SOURCE_DIR}/examples/animation_3d/demi.project.json
    --max-frames 10
    --input-replay
      ${CMAKE_SOURCE_DIR}/examples/animation_3d/replays/select_animations.replay.json
)
set_tests_properties(demi-runtime-animation-3d-selection-replay PROPERTIES
  ENVIRONMENT "DEMI_HEADLESS=1"
  PASS_REGULAR_EXPRESSION "Selected animation: Jump_Loop"
)
add_test(NAME demi-runtime-minimal-voxel-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/demi.project.json --max-frames 1
)
set_tests_properties(demi-runtime-minimal-voxel-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-runtime-animated-main-menu-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/main_menu_animated/demi.project.json --max-frames 3
)
set_tests_properties(demi-runtime-animated-main-menu-frame PROPERTIES
  ENVIRONMENT "DEMI_HEADLESS=1"
  PASS_REGULAR_EXPRESSION "Loaded data-driven menu copy revision 1")
add_test(NAME demi-runtime-gif-main-menu-frame
  COMMAND demi run --project ${CMAKE_SOURCE_DIR}/examples/main_menu_gif/demi.project.json --max-frames 3
)
set_tests_properties(demi-runtime-gif-main-menu-frame PROPERTIES ENVIRONMENT "DEMI_HEADLESS=1")
add_test(NAME demi-validate-minimal-2d-networking
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/demi.project.json
)
add_test(NAME demi-validate-multiplayer-ffa-shooter
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/multiplayer_ffa_shooter/demi.project.json
)
add_test(NAME demi-package-android-multiplayer-ffa-shooter
  COMMAND demi build apk
    --project ${CMAKE_SOURCE_DIR}/examples/multiplayer_ffa_shooter/demi.project.json
)
set_tests_properties(demi-package-android-multiplayer-ffa-shooter PROPERTIES
  RUN_SERIAL TRUE
  LABELS "packaging;android;capability-gate")
add_test(NAME demi-validate-saves-simulation-debugging
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/saves_simulation_debugging/demi.project.json
)
add_test(NAME demi-validate-isometric-base-builder
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/isometric_base_builder/demi.project.json
)
add_test(NAME demi-validate-fighting-game-2d
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/fighting_game_2d/demi.project.json
)
add_test(NAME demi-validate-chess
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/chess/demi.project.json
)
add_test(NAME demi-chess-rules-and-engine
  COMMAND demi package test ${CMAKE_SOURCE_DIR}/examples/chess
)
set_tests_properties(demi-chess-rules-and-engine PROPERTIES
  LABELS "examples;gameplay;chess")
foreach(script_name game chess/rules chess/engine chess/view)
  string(REPLACE "/" "-" script_test_name "${script_name}")
  add_test(NAME demi-script-check-chess-${script_test_name}
    COMMAND demi script check
      ${CMAKE_SOURCE_DIR}/examples/chess/scripts/${script_name}.lua
  )
endforeach()
add_test(NAME demi-validate-production-2d-foundation
  COMMAND demi validate
    ${CMAKE_SOURCE_DIR}/examples/production_2d_foundation/demi.project.json
)
add_test(NAME demi-validate-physics-2d-galton-board
  COMMAND demi validate
    ${CMAKE_SOURCE_DIR}/examples/physics_2d_galton_board/demi.project.json
)
foreach(script_name board game)
  add_test(NAME demi-script-check-physics-2d-galton-board-${script_name}
    COMMAND demi script check
      ${CMAKE_SOURCE_DIR}/examples/physics_2d_galton_board/scripts/${script_name}.lua
  )
endforeach()
add_test(NAME demi-script-check-production-2d-foundation
  COMMAND demi script check
    ${CMAKE_SOURCE_DIR}/examples/production_2d_foundation/scripts/demo.lua
)
add_test(NAME demi-script-check-fighting-game-2d
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/fighting_game_2d/scripts/fight.lua
)
add_test(NAME demi-script-check-runtime-input-buffer
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/scripts/runtime/demi/input_buffer.lua
)
add_test(NAME demi-script-check-runtime-command-recognizer
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/scripts/runtime/demi/command_recognizer.lua
)
foreach(controller top_down_controller_2d click_move_controller_2d)
  add_test(NAME demi-script-check-runtime-${controller}
    COMMAND demi script check
      ${CMAKE_SOURCE_DIR}/scripts/runtime/demi/${controller}.lua)
endforeach()
foreach(data_module flags conditions inventory quests dialogue)
  add_test(NAME demi-script-check-runtime-data-${data_module}
    COMMAND demi script check
      ${CMAKE_SOURCE_DIR}/scripts/runtime/demi/data/${data_module}.lua)
endforeach()
set(DEMI_ISOMETRIC_BASE_BUILDER_SCRIPTS
  game
  game/actions
  game/building
  game/combat
  game/config
  game/health_bars
  game/persistence
  game/projectiles
  game/selection
  game/state
  game/ui
  game/waves
)
foreach(script_name IN LISTS DEMI_ISOMETRIC_BASE_BUILDER_SCRIPTS)
  add_test(NAME demi-script-check-isometric-base-builder-${script_name}
    COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/isometric_base_builder/scripts/${script_name}.lua
  )
endforeach()
add_test(NAME demi-script-check-saves-simulation-debugging
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/saves_simulation_debugging/scripts/save_probe.lua
)
add_test(NAME demi-validate-minimal-2d-android
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_2d_android/demi.project.json
)
add_test(NAME demi-validate-minimal-3d
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_3d/demi.project.json
)
add_test(NAME demi-validate-animation-3d
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/animation_3d/demi.project.json
)
add_test(NAME demi-validate-minimal-voxel
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/demi.project.json
)
add_test(NAME demi-validate-animated-main-menu
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/main_menu_animated/demi.project.json
)
add_test(NAME demi-cook-data-driven-main-menu
  COMMAND demi cook
    --project ${CMAKE_SOURCE_DIR}/examples/main_menu_animated/demi.project.json
    --platform linux
    --output ${CMAKE_BINARY_DIR}/generated/ctest-data-driven-main-menu)
add_test(NAME demi-runtime-cooked-data-driven-main-menu
  COMMAND demi run
    --project ${CMAKE_BINARY_DIR}/generated/ctest-data-driven-main-menu/demi.project.json
    --max-frames 3)
set_tests_properties(demi-runtime-cooked-data-driven-main-menu PROPERTIES
  DEPENDS demi-cook-data-driven-main-menu
  ENVIRONMENT "DEMI_HEADLESS=1"
  PASS_REGULAR_EXPRESSION "Loaded data-driven menu copy revision 1")
add_test(NAME demi-validate-gif-main-menu
  COMMAND demi validate ${CMAKE_SOURCE_DIR}/examples/main_menu_gif/demi.project.json
)
add_test(NAME demi-script-check-minimal-player
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_2d_networking/scripts/player.lua
)
add_test(NAME demi-script-check-minimal-voxel-fly-camera
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/scripts/fly_camera.lua
)
foreach(script_name minimap_camera presentation)
  add_test(NAME demi-script-check-minimal-voxel-${script_name}
    COMMAND demi script check
      ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/scripts/${script_name}.lua
  )
endforeach()
add_test(NAME demi-script-check-minimal-voxel-worldgen
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/scripts/worldgen.lua
)
foreach(script_name
  biomes
  config
  terrain
  mesh
  chunks
  interaction
  inventory
  performance
  state
)
  add_test(NAME demi-script-check-minimal-voxel-worldgen-${script_name}
    COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/scripts/worldgen/${script_name}.lua
  )
endforeach()
add_test(NAME demi-script-check-minimal-voxel-import-pack
  COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_voxel/tools/import_pack.lua
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
set(DEMI_MULTIPLAYER_FFA_SHOOTER_SCRIPTS
  game
  shooter/actions
  shooter/combat
  shooter/config
  shooter/hud
  shooter/movement
  shooter/session
)
foreach(script_name IN LISTS DEMI_MULTIPLAYER_FFA_SHOOTER_SCRIPTS)
  add_test(NAME demi-script-check-multiplayer-ffa-shooter-${script_name}
    COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/multiplayer_ffa_shooter/scripts/${script_name}.lua
  )
endforeach()
set(DEMI_ANDROID_EXAMPLE_SCRIPTS
  game
  game/collectibles
  game/hud
  game/platforms
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
  network_lobby
  network_replication
  player
  player_colors
  player_config
  player_platformer
  player_slingshot
)
foreach(script_name IN LISTS DEMI_ANDROID_EXAMPLE_SCRIPTS)
  add_test(NAME demi-script-check-android-${script_name}
    COMMAND demi script check ${CMAKE_SOURCE_DIR}/examples/minimal_2d_android/scripts/${script_name}.lua
  )
endforeach()
add_test(NAME demi-lua-stubs-generate
  COMMAND demi lua-stubs generate ${CMAKE_BINARY_DIR}/generated/demi.lua
)
