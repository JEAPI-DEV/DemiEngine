# Unit, subsystem, CLI, and networking test registration.
add_test(NAME demi-smoke-tests COMMAND demi-smoke-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-project-templates-tests COMMAND demi-project-templates-tests)
add_test(NAME demi-editor-workspace-tests COMMAND demi-editor-workspace-tests)
add_test(NAME demi-editor-scene-view-state-tests
  COMMAND demi-editor-scene-view-state-tests)
add_test(NAME demi-editor-viewport-tool-tests
  COMMAND demi-editor-viewport-tool-tests)
add_test(NAME demi-editor-scene-document-tests
  COMMAND demi-editor-scene-document-tests)
add_test(NAME demi-editor-scene-commands-tests
  COMMAND demi-editor-scene-commands-tests)
add_test(NAME demi-editor-authoring-workflow-tests
  COMMAND demi-editor-authoring-workflow-tests)
add_test(NAME demi-editor-inspector-model-tests
  COMMAND demi-editor-inspector-model-tests)
add_test(NAME demi-editor-imgui-input-tests
  COMMAND demi-editor-imgui-input-tests)
add_test(NAME demi-editor-help COMMAND demi-editor --help)
add_test(NAME demi-doctor-service-tests COMMAND demi-doctor-service-tests)
add_test(NAME demi-package-manager-tests COMMAND demi-package-manager-tests)
foreach(package_name IN ITEMS
    core controllers health projectiles interactions traversal camera inventory encounters)
  add_test(NAME demi-gameplay-package-${package_name}
    COMMAND demi package test
      ${CMAKE_SOURCE_DIR}/packages/sources/demi.gameplay.${package_name})
  set_tests_properties(demi-gameplay-package-${package_name}
    PROPERTIES LABELS "packages;gameplay")
endforeach()
add_test(NAME demi-network-lobby-package
  COMMAND demi package test
    ${CMAKE_SOURCE_DIR}/packages/sources/demi.network.lobby)
set_tests_properties(demi-network-lobby-package
  PROPERTIES LABELS "packages;network")
add_test(NAME demi-project-watch-reload-tests
  COMMAND demi-project-watch-reload-tests)
add_test(NAME demi-capability-manifest-tests
  COMMAND demi-capability-manifest-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-runtime-object-model-tests
  COMMAND demi-runtime-object-model-tests)
add_test(NAME demi-runtime-scene-prefab-tests
  COMMAND demi-runtime-scene-prefab-tests)
add_test(NAME demi-runtime-lifetime-failure-tests
  COMMAND demi-runtime-lifetime-failure-tests)
add_test(NAME demi-runtime-asset-service-tests
  COMMAND demi-runtime-asset-service-tests)
set_tests_properties(demi-runtime-lifetime-failure-tests
  PROPERTIES LABELS "lifetime")
add_test(NAME demi-capabilities-export
  COMMAND demi capabilities export
    --output ${CMAKE_BINARY_DIR}/generated/capabilities.json)
add_test(NAME demi-capabilities-check
  COMMAND demi capabilities check)
add_test(NAME demi-capabilities-reference-gates
  COMMAND demi capabilities verify-gates)
add_test(NAME demi-physics2d-tests COMMAND demi-physics2d-tests)
add_test(NAME demi-physics3d-tests COMMAND demi-physics3d-tests)
add_test(NAME demi-transform3d-hierarchy-tests
  COMMAND demi-transform3d-hierarchy-tests)
add_test(NAME demi-validation-3d-tests COMMAND demi-validation-3d-tests)
add_test(NAME demi-render-phase6-tests COMMAND demi-render-phase6-tests)
add_test(NAME demi-job-system-tests COMMAND demi-job-system-tests)
add_test(NAME demi-bgfx-graphics-device-tests
  COMMAND demi-bgfx-graphics-device-tests)
add_test(NAME demi-platform-input-tests COMMAND demi-platform-input-tests)
add_test(NAME demi-sdl-platform-host-tests COMMAND demi-sdl-platform-host-tests)
add_test(NAME demi-resource-handle-tests COMMAND demi-resource-handle-tests)
add_test(NAME demi-bgfx-gpu-resources-tests COMMAND demi-bgfx-gpu-resources-tests)
add_test(NAME demi-quad-batch-tests COMMAND demi-quad-batch-tests)
add_test(NAME demi-render-commands-tests COMMAND demi-render-commands-tests)
add_test(NAME demi-canvas2d-tests COMMAND demi-canvas2d-tests)
add_test(NAME demi-font-atlas2d-tests COMMAND demi-font-atlas2d-tests)
add_test(NAME demi-image-decoder2d-tests COMMAND demi-image-decoder2d-tests)
add_test(NAME demi-ui-canvas-renderer-tests
  COMMAND demi-ui-canvas-renderer-tests)
add_test(NAME demi-color-packing2d-tests COMMAND demi-color-packing2d-tests)
add_test(NAME demi-collider-canvas-renderer-tests
  COMMAND demi-collider-canvas-renderer-tests)
add_test(NAME demi-texture-library2d-tests
  COMMAND demi-texture-library2d-tests)
add_test(NAME demi-sprite-canvas-renderer-tests
  COMMAND demi-sprite-canvas-renderer-tests)
add_test(NAME demi-tilemap-canvas-renderer-tests
  COMMAND demi-tilemap-canvas-renderer-tests)
add_test(NAME demi-bgfx2d-effect-renderers-tests
  COMMAND demi-bgfx2d-effect-renderers-tests)
add_test(NAME demi-bgfx-renderer2d-tests COMMAND demi-bgfx-renderer2d-tests)
add_test(NAME demi-material-library-tests COMMAND demi-material-library-tests)
add_test(NAME demi-primitive-canvas3d-tests COMMAND demi-primitive-canvas3d-tests)
add_test(NAME demi-debug-geometry3d-tests COMMAND demi-debug-geometry3d-tests)
add_test(NAME demi-gpu-mesh3d-tests COMMAND demi-gpu-mesh3d-tests)
add_test(NAME demi-mesh-geometry3d-tests COMMAND demi-mesh-geometry3d-tests)
add_test(NAME demi-bgfx-renderer3d-tests COMMAND demi-bgfx-renderer3d-tests)
add_test(NAME demi-bgfx-scene-extraction-tests
  COMMAND demi-bgfx-scene-extraction-tests)
add_test(NAME demi-scene-visibility3d-tests
  COMMAND demi-scene-visibility3d-tests)
add_test(NAME demi-gltf-skinned-model-tests COMMAND demi-gltf-skinned-model-tests)
add_test(NAME demi-bgfx-2d-app-host-tests
  COMMAND demi-bgfx-2d-app-host-tests)
add_test(NAME demi-bgfx-3d-app-host-tests
  COMMAND demi-bgfx-3d-app-host-tests)
add_test(NAME demi-iso-canvas-renderer-tests
  COMMAND demi-iso-canvas-renderer-tests)
add_test(NAME demi-lua-stub-contract-tests COMMAND demi-lua-stub-contract-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-lua-scripting-tests COMMAND demi-lua-scripting-tests)
add_test(NAME demi-script-property-contract-tests
  COMMAND demi-script-property-contract-tests)
add_test(NAME demi-scene-loader-tests COMMAND demi-scene-loader-tests ${CMAKE_SOURCE_DIR})
add_test(NAME demi-prefab-tests COMMAND demi-prefab-tests)
add_test(NAME demi-ui-tests COMMAND demi-ui-tests)
add_test(NAME demi-ui-step3-tests COMMAND demi-ui-step3-tests)
add_test(NAME demi-regex-matcher-tests COMMAND demi-regex-matcher-tests)
add_test(NAME demi-input-action-tests COMMAND demi-input-action-tests)
add_test(NAME demi-sprite-animation-tests COMMAND demi-sprite-animation-tests)
add_test(NAME demi-animation-primitives-tests COMMAND demi-animation-primitives-tests)
add_test(NAME demi-animation-phase7-tests COMMAND demi-animation-phase7-tests)
add_test(NAME demi-audio-phase7-tests COMMAND demi-audio-phase7-tests)
add_test(NAME demi-asset-pipeline-tests COMMAND demi-asset-pipeline-tests)
add_test(NAME demi-asset-streaming-tests COMMAND demi-asset-streaming-tests)
add_test(NAME demi-lightweight-3d-workflow-tests
  COMMAND demi-lightweight-3d-workflow-tests)
add_test(NAME demi-data-asset-tests COMMAND demi-data-asset-tests)
add_test(NAME demi-lua-data-bindings-tests COMMAND demi-lua-data-bindings-tests)
add_test(NAME demi-shader-cooker-tests COMMAND demi-shader-cooker-tests)
add_test(NAME demi-camera2d-tests COMMAND demi-camera2d-tests)
add_test(NAME demi-tilemap-tests COMMAND demi-tilemap-tests)
add_test(NAME demi-simulation-tests COMMAND demi-simulation-tests)
  add_test(NAME demi-input-replay-tests COMMAND demi-input-replay-tests)

  add_test(NAME demi-game-save-document-tests COMMAND demi-game-save-document-tests)
  add_test(NAME demi-runtime-profiler-tests COMMAND demi-runtime-profiler-tests)
  add_test(NAME demi-profiler-hud-layout-tests COMMAND demi-profiler-hud-layout-tests)
  add_test(NAME demi-isometric-grid-tests COMMAND demi-isometric-grid-tests)
  add_test(NAME demi-navigation2d-tests COMMAND demi-navigation2d-tests)
add_test(NAME demi-prefab-inspect-cli
  COMMAND demi prefab inspect ${CMAKE_SOURCE_DIR}/examples/minimal_3d/prefabs/player.prefab.json)
add_test(NAME demi-scene-expand-cli
  COMMAND demi scene expand ${CMAKE_SOURCE_DIR}/examples/minimal_3d/scenes/main.scene.json)
add_test(NAME demi-scene-diff-cli
  COMMAND demi scene diff ${CMAKE_SOURCE_DIR}/examples/minimal_3d/scenes/main.scene.json ${CMAKE_SOURCE_DIR}/examples/minimal_3d/scenes/main.scene.json)
add_test(NAME demi-network-session-lua-tests COMMAND demi-network-session-lua-tests)
add_test(NAME demi-game-network-session-tests COMMAND demi-game-network-session-tests)
add_test(NAME demi-secure-network-session-tests COMMAND demi-secure-network-session-tests)
if(DEMI_ENABLE_NETWORK)
  add_test(NAME demi-server-headless-ffa-smoke
    COMMAND demi-server run --project
      ${CMAKE_SOURCE_DIR}/examples/multiplayer_ffa_shooter/demi.project.json
      --max-frames 3)
  add_test(NAME demi-network-tests COMMAND demi-network-tests)
  add_test(NAME demi-network-two-client-tests COMMAND demi-network-two-client-tests)
  add_test(
    NAME demi-lua-server-tests
    COMMAND demi-lua-server-tests
      $<TARGET_FILE:demi>
      ${CMAKE_SOURCE_DIR}/examples/minimal_2d_android_server/demi.project.json
      ${mbedtls_SOURCE_DIR}/framework/data_files/server5.crt
      ${mbedtls_SOURCE_DIR}/framework/data_files/server5.key
      ${mbedtls_SOURCE_DIR}/framework/data_files/test-ca2.crt
      localhost
  )
endif()
