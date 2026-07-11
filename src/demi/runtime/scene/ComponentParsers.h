#pragma once

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

namespace demi::runtime::scene_loading::component_parsing {

void parseTransform2D(const Json &json, Entity &entity);
void parseCamera2D(const Json &json, Entity &entity);
void parseSprite(const Json &json, Entity &entity);
void parseIsoGrid(const Json &json, Entity &entity);
void parseIsoTransform(const Json &json, Entity &entity);
void parseHitboxController(const Json &json, Entity &entity);
void parseLuaScript(const Json &json, Entity &entity);
void parseBuildable(const Json &json, Entity &entity);
void parseRigidbody2D(const Json &json, Entity &entity);
void parseBoxCollider2D(const Json &json, Entity &entity);
void parseTransform3D(const Json &json, Entity &entity);
void parseCamera3D(const Json &json, Entity &entity);
void parseMeshRenderer(const Json &json, Entity &entity);
void parseAnimationPlayer3D(const Json &json, Entity &entity);
void parseBoxCollider3D(const Json &json, Entity &entity);
void parseSphereCollider3D(const Json &json, Entity &entity);
void parseRigidbody3D(const Json &json, Entity &entity);
void parseDirectionalLight(const Json &json, Entity &entity);
void parseAudioSource(const Json &json, Entity &entity);
void parseAudioListener(const Json &json, Entity &entity);
void parseVideoPlayer(const Json &json, Entity &entity);

} // namespace demi::runtime::scene_loading::component_parsing
