#include "demi/schema/Validation.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool hasCode(const demi::Diagnostics &diagnostics, const std::string &code) {
  return std::ranges::any_of(diagnostics, [&](const demi::Diagnostic &entry) {
    return entry.code == code;
  });
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("demi_validation_3d_" + std::to_string(nonce));
  std::filesystem::create_directories(root);
  const auto scene = root / "hierarchy.scene.json";
  {
    std::ofstream output(scene);
    output << R"({"format_version":1,"id":"scene://test","entities":[
      {"id":"a","components":{"Transform3D":{"parent":"b"}}},
      {"id":"b","components":{"Transform3D":{"parent":"a"}}},
      {"id":"missing_child","components":{"Transform3D":{"parent":"gone"}}}
    ]})";
  }
  const auto diagnostics =
      demi::validateTextFile(scene, demi::SourceFileKind::Scene);
  if (!hasCode(diagnostics, "TRANSFORM3D_HIERARCHY_CYCLE") ||
      !hasCode(diagnostics, "TRANSFORM3D_PARENT_NOT_FOUND")) {
    std::cerr << "3D hierarchy validation diagnostics were not emitted.\n";
    return 1;
  }

  const auto physicsScene = root / "invalid_physics.scene.json";
  {
    std::ofstream output(physicsScene);
    output << R"({"format_version":1,"id":"scene://physics","entities":[
      {"id":"dynamic_mesh","components":{
        "Transform3D":{},
        "ModelCollider3D":{"asset":"asset://colliders/level"},
        "Rigidbody3D":{"body_type":"dynamic"}
      }},
      {"id":"parented","components":{
        "Transform3D":{"parent":"root"},
        "BoxCollider3D":{},
        "Rigidbody3D":{"body_type":"kinematic"}
      }},
      {"id":"root","components":{"Transform3D":{}}},
      {"id":"bad_capsule","components":{
        "Transform3D":{},
        "CapsuleCollider3D":{"radius":1.0,"height":1.0}
      }},
      {"id":"bad_convex","components":{
        "Transform3D":{},
        "ConvexCollider3D":{"points":[[0,0,0],[1,0,0],[0,1,0]]}
      }},
      {"id":"two_shapes","components":{
        "Transform3D":{},
        "BoxCollider3D":{},
        "SphereCollider3D":{}
      }},
      {"id":"no_shape","components":{
        "Transform3D":{},
        "Rigidbody3D":{"body_type":"dynamic"}
      }},
      {"id":"character_no_shape","components":{
        "Transform3D":{},
        "CharacterController3D":{}
      }},
      {"id":"character_body","components":{
        "Transform3D":{},
        "BoxCollider3D":{},
        "CharacterController3D":{},
        "Rigidbody3D":{"body_type":"dynamic"}
      }},
      {"id":"character_mesh","components":{
        "Transform3D":{},
        "ModelCollider3D":{"asset":"asset://colliders/level"},
        "CharacterController3D":{}
      }},
      {"id":"character_trigger","components":{
        "Transform3D":{},
        "BoxCollider3D":{"is_trigger":true},
        "CharacterController3D":{}
      }},
      {"id":"character_child","components":{
        "Transform3D":{"parent":"root"},
        "BoxCollider3D":{},
        "CharacterController3D":{}
      }}
    ]})";
  }
  const auto physicsDiagnostics =
      demi::validateTextFile(physicsScene, demi::SourceFileKind::Scene);
  for (const std::string code :
       {"PHYSICS3D_MESH_REQUIRES_STATIC_BODY",
        "PHYSICS3D_MOVING_BODY_REQUIRES_ROOT_TRANSFORM",
        "PHYSICS3D_CAPSULE_HEIGHT_TOO_SMALL",
        "PHYSICS3D_CONVEX_REQUIRES_FOUR_POINTS", "PHYSICS3D_MULTIPLE_COLLIDERS",
        "PHYSICS3D_BODY_REQUIRES_COLLIDER",
        "PHYSICS3D_CHARACTER_REQUIRES_COLLIDER",
        "PHYSICS3D_CHARACTER_CONFLICTS_WITH_BODY",
        "PHYSICS3D_CHARACTER_REQUIRES_CONVEX_COLLIDER",
        "PHYSICS3D_CHARACTER_COLLIDER_CANNOT_BE_TRIGGER",
        "PHYSICS3D_CHARACTER_REQUIRES_ROOT_TRANSFORM"}) {
    if (!hasCode(physicsDiagnostics, code)) {
      std::cerr << "Missing expected 3D physics diagnostic: " << code << '\n';
      std::filesystem::remove_all(root);
      return 1;
    }
  }

  const auto validScene = root / "valid_physics.scene.json";
  {
    std::ofstream output(validScene);
    output << R"({"format_version":1,"id":"scene://valid","entities":[
      {"id":"floor","components":{
        "Transform3D":{},
        "ModelCollider3D":{"asset":"asset://colliders/level"},
        "Rigidbody3D":{"body_type":"static"}
      }},
      {"id":"crate","components":{
        "Transform3D":{},
        "ConvexCollider3D":{"points":[[0,0,0],[1,0,0],[0,1,0],[0,0,1]]},
        "Rigidbody3D":{"body_type":"dynamic"}
      }},
      {"id":"player","components":{
        "Transform3D":{},
        "BoxCollider3D":{"size":[1,1,1],"layer":"player"},
        "CharacterController3D":{"step_height":0.3}
      }}
    ]})";
  }
  const auto validDiagnostics =
      demi::validateTextFile(validScene, demi::SourceFileKind::Scene);
  if (hasCode(validDiagnostics, "PHYSICS3D_MESH_REQUIRES_STATIC_BODY") ||
      hasCode(validDiagnostics, "PHYSICS3D_CONVEX_REQUIRES_FOUR_POINTS")) {
    std::cerr << "Valid 3D collider combinations were rejected.\n";
    std::filesystem::remove_all(root);
    return 1;
  }
  const auto invalidDebugScene = root / "invalid_debug.scene.json";
  {
    std::ofstream output(invalidDebugScene);
    output << R"({"format_version":1,"id":"scene://debug","entities":[
      {"id":"camera","components":{"Transform3D":{},
       "Camera3D":{"debug_mode":"renderer-secret"}}}
    ]})";
  }
  if (!hasCode(demi::validateTextFile(invalidDebugScene,
                                      demi::SourceFileKind::Scene),
               "SCENE_INVALID_COMPONENT_FIELD")) {
    std::cerr << "Unknown renderer diagnostic modes were not rejected.\n";
    std::filesystem::remove_all(root);
    return 1;
  }
  std::filesystem::remove_all(root);
  return 0;
}
