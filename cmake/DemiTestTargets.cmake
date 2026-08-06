# Native test executables. Test registration lives in dedicated modules.
  add_executable(demi-smoke-tests tests/smoke_tests.cpp)
  target_link_libraries(demi-smoke-tests PRIVATE demi-core)

  add_executable(demi-project-templates-tests tests/project_templates_tests.cpp)
  target_link_libraries(demi-project-templates-tests PRIVATE demi-cli-support)
  target_compile_definitions(demi-project-templates-tests PRIVATE
    DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_executable(demi-doctor-service-tests tests/doctor_service_tests.cpp)
  target_link_libraries(demi-doctor-service-tests PRIVATE demi-cli-support)
  target_compile_definitions(demi-doctor-service-tests PRIVATE
    DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_executable(demi-project-watch-reload-tests
    tests/project_watch_reload_tests.cpp)
  target_link_libraries(demi-project-watch-reload-tests PRIVATE demi-runtime-lib)

  add_executable(demi-capability-manifest-tests
    tests/capability_manifest_tests.cpp)
  target_link_libraries(demi-capability-manifest-tests PRIVATE demi-core)
  add_executable(demi-runtime-object-model-tests
    tests/runtime_object_model_tests.cpp)
  target_link_libraries(demi-runtime-object-model-tests PRIVATE demi-core)
  add_executable(demi-runtime-scene-prefab-tests
    tests/runtime_scene_prefab_tests.cpp)
  target_link_libraries(demi-runtime-scene-prefab-tests PRIVATE demi-runtime-lib)
  target_compile_definitions(demi-runtime-scene-prefab-tests
    PRIVATE DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_executable(demi-runtime-lifetime-failure-tests
    tests/runtime_lifetime_failure_tests.cpp)
  target_link_libraries(demi-runtime-lifetime-failure-tests
    PRIVATE demi-runtime-lib)
  target_compile_definitions(demi-runtime-lifetime-failure-tests
    PRIVATE DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  add_executable(demi-physics2d-tests tests/physics2d_tests.cpp)
  target_link_libraries(demi-physics2d-tests PRIVATE demi-runtime-lib)

  add_executable(demi-physics3d-tests tests/physics3d_tests.cpp)
  target_link_libraries(demi-physics3d-tests PRIVATE demi-runtime-lib)
  add_executable(demi-transform3d-hierarchy-tests
    tests/transform3d_hierarchy_tests.cpp)
  target_link_libraries(demi-transform3d-hierarchy-tests PRIVATE demi-core)
  add_executable(demi-validation-3d-tests tests/validation_3d_tests.cpp)
  target_link_libraries(demi-validation-3d-tests PRIVATE demi-core)
  add_executable(demi-render-phase6-tests tests/render_phase6_tests.cpp)
  target_link_libraries(demi-render-phase6-tests PRIVATE demi-runtime-lib)
  add_executable(demi-bgfx-graphics-device-tests
    tests/bgfx_graphics_device_tests.cpp)
  target_link_libraries(demi-bgfx-graphics-device-tests
    PRIVATE demi-graphics-bgfx)
  add_executable(demi-platform-input-tests tests/platform_input_tests.cpp)
  target_link_libraries(demi-platform-input-tests PRIVATE demi-runtime-lib)
  add_executable(demi-sdl-platform-host-tests
    tests/sdl_platform_host_tests.cpp)
  target_link_libraries(demi-sdl-platform-host-tests
    PRIVATE demi-runtime-lib SDL3::SDL3-static)
  add_executable(demi-resource-handle-tests tests/resource_handle_tests.cpp)
  target_link_libraries(demi-resource-handle-tests PRIVATE demi-graphics-bgfx)
  add_executable(demi-bgfx-gpu-resources-tests
    tests/bgfx_gpu_resources_tests.cpp)
  target_link_libraries(demi-bgfx-gpu-resources-tests
    PRIVATE demi-graphics-bgfx)
  add_executable(demi-quad-batch-tests tests/quad_batch_tests.cpp)
  target_link_libraries(demi-quad-batch-tests PRIVATE demi-graphics-bgfx)
  add_executable(demi-render-commands-tests tests/render_commands_tests.cpp)
  target_link_libraries(demi-render-commands-tests PRIVATE demi-graphics-bgfx)
  add_executable(demi-canvas2d-tests tests/canvas2d_tests.cpp)
  target_link_libraries(demi-canvas2d-tests PRIVATE demi-graphics-bgfx)
  add_executable(demi-font-atlas2d-tests tests/font_atlas2d_tests.cpp)
  target_link_libraries(demi-font-atlas2d-tests PRIVATE demi-graphics-bgfx)
  add_executable(demi-image-decoder2d-tests tests/image_decoder2d_tests.cpp)
  target_link_libraries(demi-image-decoder2d-tests PRIVATE demi-graphics-bgfx)
  add_executable(demi-ui-canvas-renderer-tests
    tests/ui_canvas_renderer_tests.cpp)
  target_link_libraries(demi-ui-canvas-renderer-tests PRIVATE demi-render2d-bgfx)
  add_executable(demi-color-packing2d-tests
    tests/color_packing2d_tests.cpp)
  target_link_libraries(demi-color-packing2d-tests PRIVATE demi-render2d-bgfx)
  add_executable(demi-collider-canvas-renderer-tests
    tests/collider_canvas_renderer_tests.cpp)
  target_link_libraries(demi-collider-canvas-renderer-tests
    PRIVATE demi-render2d-bgfx)
  add_executable(demi-texture-library2d-tests
    tests/texture_library2d_tests.cpp)
  target_link_libraries(demi-texture-library2d-tests PRIVATE demi-graphics-bgfx)
  add_executable(demi-sprite-canvas-renderer-tests
    tests/sprite_canvas_renderer_tests.cpp)
  target_link_libraries(demi-sprite-canvas-renderer-tests
    PRIVATE demi-render2d-bgfx)
  add_executable(demi-tilemap-canvas-renderer-tests
    tests/tilemap_canvas_renderer_tests.cpp)
  target_link_libraries(demi-tilemap-canvas-renderer-tests
    PRIVATE demi-render2d-bgfx)
  add_executable(demi-bgfx2d-effect-renderers-tests
    tests/bgfx2d_effect_renderers_tests.cpp)
  target_link_libraries(demi-bgfx2d-effect-renderers-tests
    PRIVATE demi-render2d-bgfx)
  add_executable(demi-bgfx-renderer2d-tests tests/bgfx_renderer2d_tests.cpp)
  target_link_libraries(demi-bgfx-renderer2d-tests PRIVATE demi-runtime-lib)
  target_compile_definitions(demi-bgfx-renderer2d-tests PRIVATE
    DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_executable(demi-material-library-tests tests/material_library_tests.cpp)
  target_link_libraries(demi-material-library-tests PRIVATE demi-render2d-bgfx)
  add_executable(demi-primitive-canvas3d-tests
    tests/primitive_canvas3d_tests.cpp)
  target_link_libraries(demi-primitive-canvas3d-tests
    PRIVATE demi-render3d-bgfx)
  add_executable(demi-gpu-mesh3d-tests tests/gpu_mesh3d_tests.cpp)
  target_link_libraries(demi-gpu-mesh3d-tests PRIVATE demi-render3d-bgfx)
  add_executable(demi-mesh-geometry3d-tests tests/mesh_geometry3d_tests.cpp)
  target_link_libraries(demi-mesh-geometry3d-tests PRIVATE demi-render3d-bgfx)
  add_executable(demi-bgfx-renderer3d-tests tests/bgfx_renderer3d_tests.cpp)
  target_link_libraries(demi-bgfx-renderer3d-tests PRIVATE demi-render3d-bgfx)
  target_compile_definitions(demi-bgfx-renderer3d-tests PRIVATE
    DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_executable(demi-bgfx-scene-extraction-tests
    tests/bgfx_scene_extraction_tests.cpp)
  target_link_libraries(demi-bgfx-scene-extraction-tests
    PRIVATE demi-render3d-bgfx)
  add_executable(demi-gltf-skinned-model-tests
    tests/gltf_skinned_model_tests.cpp)
  target_link_libraries(demi-gltf-skinned-model-tests PRIVATE demi-core)
  target_compile_definitions(demi-gltf-skinned-model-tests PRIVATE
    DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_executable(demi-bgfx-2d-app-host-tests
    tests/bgfx_2d_app_host_tests.cpp)
  target_link_libraries(demi-bgfx-2d-app-host-tests PRIVATE demi-runtime-lib)
  add_executable(demi-bgfx-3d-app-host-tests
    tests/bgfx_3d_app_host_tests.cpp)
  target_link_libraries(demi-bgfx-3d-app-host-tests PRIVATE demi-runtime-lib)
  target_compile_definitions(demi-bgfx-3d-app-host-tests PRIVATE
    DEMI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_executable(demi-iso-canvas-renderer-tests
    tests/iso_canvas_renderer_tests.cpp)
  target_link_libraries(demi-iso-canvas-renderer-tests
    PRIVATE demi-render2d-bgfx)

  add_executable(demi-lua-stub-contract-tests tests/lua_stub_contract_tests.cpp)
  target_link_libraries(demi-lua-stub-contract-tests PRIVATE demi-runtime-lib)

  add_executable(demi-lua-scripting-tests tests/lua_scripting_tests.cpp)
  target_link_libraries(demi-lua-scripting-tests PRIVATE demi-runtime-lib)

  add_executable(demi-scene-loader-tests tests/scene_loader_tests.cpp)
  target_link_libraries(demi-scene-loader-tests PRIVATE demi-runtime-lib)
  add_executable(demi-prefab-tests tests/prefab_tests.cpp)
  target_link_libraries(demi-prefab-tests PRIVATE demi-core)
  add_executable(demi-ui-tests tests/ui_tests.cpp)
  target_link_libraries(demi-ui-tests PRIVATE demi-core)
  add_executable(demi-regex-matcher-tests tests/regex_matcher_tests.cpp)
  target_link_libraries(demi-regex-matcher-tests PRIVATE demi-runtime-lib)
  add_executable(demi-input-action-tests tests/input_action_tests.cpp)
  target_link_libraries(demi-input-action-tests PRIVATE demi-core)
  add_executable(demi-sprite-animation-tests tests/sprite_animation_tests.cpp)
  target_link_libraries(demi-sprite-animation-tests PRIVATE demi-runtime-lib)
  add_executable(demi-animation-primitives-tests tests/animation_primitives_tests.cpp)
  target_link_libraries(demi-animation-primitives-tests PRIVATE demi-runtime-lib)
  add_executable(demi-animation-phase7-tests tests/animation_phase7_tests.cpp)
  target_link_libraries(demi-animation-phase7-tests PRIVATE demi-runtime-lib)
  add_executable(demi-audio-phase7-tests tests/audio_phase7_tests.cpp)
  target_link_libraries(demi-audio-phase7-tests PRIVATE demi-runtime-lib)
  add_executable(demi-asset-pipeline-tests tests/asset_pipeline_tests.cpp)
  target_link_libraries(demi-asset-pipeline-tests PRIVATE demi-core)
  add_executable(demi-shader-cooker-tests tests/shader_cooker_tests.cpp)
  target_link_libraries(demi-shader-cooker-tests PRIVATE demi-core)
  target_compile_definitions(demi-shader-cooker-tests PRIVATE
    DEMI_SHADERC_PATH="${DEMI_SHADERC_EXECUTABLE}"
    DEMI_BGFX_SHADER_INCLUDE_DIR="${bgfx_SOURCE_DIR}/bgfx/src")
  if(NOT DEMI_HOST_SHADERC)
    add_dependencies(demi-shader-cooker-tests shaderc)
  endif()
  add_executable(demi-camera2d-tests tests/camera2d_tests.cpp)
  target_link_libraries(demi-camera2d-tests PRIVATE demi-runtime-lib)
  add_executable(demi-tilemap-tests tests/tilemap_tests.cpp)
  target_link_libraries(demi-tilemap-tests PRIVATE demi-runtime-lib)
  add_executable(demi-simulation-tests tests/simulation_tests.cpp)
  target_link_libraries(demi-simulation-tests PRIVATE demi-runtime-lib)
  add_executable(demi-input-replay-tests tests/input_replay_tests.cpp)
  target_link_libraries(demi-input-replay-tests PRIVATE demi-runtime-lib)

  add_executable(demi-network-session-lua-tests tests/network_session_lua_tests.cpp)
  target_link_libraries(demi-network-session-lua-tests PRIVATE demi-runtime-lib)
  add_executable(demi-game-network-session-tests tests/game_network_session_tests.cpp)
  target_link_libraries(demi-game-network-session-tests PRIVATE demi-runtime-lib)

  if(DEMI_ENABLE_NETWORK)
    add_executable(demi-network-tests tests/network_tests.cpp)
    target_link_libraries(demi-network-tests PRIVATE demi-runtime-lib)
    target_compile_definitions(demi-network-tests PRIVATE
      DEMI_DTLS_TEST_CERT="${mbedtls_SOURCE_DIR}/framework/data_files/server5.crt"
      DEMI_DTLS_TEST_KEY="${mbedtls_SOURCE_DIR}/framework/data_files/server5.key"
      DEMI_DTLS_TEST_CA="${mbedtls_SOURCE_DIR}/framework/data_files/test-ca2.crt"
    )
    add_executable(demi-network-two-client-tests tests/network_two_client_tests.cpp)
    target_link_libraries(demi-network-two-client-tests PRIVATE demi-runtime-lib)
    add_executable(demi-lua-server-tests tests/lua_server_tests.cpp)
    target_link_libraries(demi-lua-server-tests PRIVATE demi-runtime-lib)
  endif()

  add_executable(demi-game-save-document-tests tests/game_save_document_tests.cpp)
  target_link_libraries(demi-game-save-document-tests PRIVATE demi-runtime-lib)
  add_executable(demi-runtime-profiler-tests tests/runtime_profiler_tests.cpp)
  target_link_libraries(demi-runtime-profiler-tests PRIVATE demi-runtime-lib)
  add_executable(demi-profiler-hud-layout-tests tests/profiler_hud_layout_tests.cpp)
  target_link_libraries(demi-profiler-hud-layout-tests PRIVATE demi-runtime-lib)
  add_executable(demi-isometric-grid-tests tests/isometric_grid_tests.cpp)
  target_link_libraries(demi-isometric-grid-tests PRIVATE demi-core)
  add_executable(demi-navigation2d-tests tests/navigation2d_tests.cpp)
  target_link_libraries(demi-navigation2d-tests PRIVATE demi-core)
