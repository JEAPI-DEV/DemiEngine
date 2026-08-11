#include "demi/runtime/network/ReplicatedState.h"
#include "demi/runtime/network/NetworkContract.h"

#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace demi::runtime {

namespace {

template <typename Vector> nlohmann::json vectorJson(const Vector &value) {
  if constexpr (requires { value.z; })
    return nlohmann::json::array({value.x, value.y, value.z});
  return nlohmann::json::array({value.x, value.y});
}

nlohmann::json colorJson(const Color &value) {
  return nlohmann::json::array({value.r, value.g, value.b, value.a});
}

bool validReplicatedState(const nlohmann::json &state, std::string &error) {
  if (!state.is_object()) {
    error = "replicated state must be an object";
    return false;
  }
  for (const auto &[componentName, componentState] : state.items()) {
    const auto *descriptor =
        scene_loading::findComponentDescriptor(componentName);
    if (descriptor == nullptr || !componentState.is_object()) {
      error = "unknown or invalid replicated component: " + componentName;
      return false;
    }
    for (const auto &[fieldName, value] : componentState.items()) {
      const auto field = std::ranges::find(descriptor->fields, fieldName,
                                           &ComponentFieldDescriptor::name);
      if (field == descriptor->fields.end() || !field->replicated) {
        error = componentName + "." + fieldName +
                " is not allowed in replicated state";
        return false;
      }
    }
    const auto validation =
        scene_loading::validateComponent(*descriptor, componentState);
    if (!validation.empty()) {
      error = componentName + "." + validation.front().field + " " +
              validation.front().message;
      return false;
    }
  }
  return true;
}

bool contractAllows(const NetworkPrefabRule &prefab, const std::string &path,
                    const NetworkActor writer, const nlohmann::json &value,
                    std::string &error) {
  if (value.is_object()) {
    for (const auto &[name, child] : value.items())
      if (!contractAllows(prefab, path + "." + name, writer, child, error))
        return false;
    return true;
  }
  const auto rule = prefab.fields.find(path);
  if (rule == prefab.fields.end()) {
    error = path + " is not declared by the network contract";
    return false;
  }
  if (rule->second.writeBy != writer &&
      rule->second.writeBy != NetworkActor::All) {
    error =
        path + " is not writable by " + std::string(networkActorName(writer));
    return false;
  }
  return true;
}

} // namespace

bool isReplicatedField(const std::string_view component,
                       const std::string_view field) {
  const auto *descriptor = scene_loading::findComponentDescriptor(component);
  return descriptor != nullptr &&
         std::ranges::any_of(descriptor->fields, [&](const auto &candidate) {
           return candidate.name == field && candidate.replicated;
         });
}

nlohmann::json captureReplicatedState(const Entity &entity) {
  nlohmann::json state = nlohmann::json::object();
  if (const auto *transform = entity.component<Transform2DComponent>()) {
    state["Transform2D"] = {
        {"position", vectorJson(transform->position)},
        {"rotation", transform->rotation},
        {"scale", vectorJson(transform->scale)},
    };
  }
  if (const auto *transform = entity.component<Transform3DComponent>()) {
    state["Transform3D"] = {
        {"position", vectorJson(transform->position)},
        {"rotation", vectorJson(transform->rotation)},
        {"scale", vectorJson(transform->scale)},
    };
  }
  if (const auto *body = entity.component<Rigidbody2DComponent>()) {
    state["Rigidbody2D"] = {{"velocity", vectorJson(body->velocity)}};
  }
  if (const auto *body = entity.component<Rigidbody3DComponent>()) {
    state["Rigidbody3D"] = {{"velocity", vectorJson(body->velocity)}};
  }
  if (const auto *sprite = entity.component<SpriteComponent>()) {
    state["Sprite"] = {
        {"flip_x", sprite->flipX},
        {"flip_y", sprite->flipY},
        {"color", colorJson(sprite->color)},
    };
  }
  return state;
}

nlohmann::json captureContractReplicatedState(const Entity &entity,
                                              const NetworkContract &contract,
                                              const std::string_view prefabKey,
                                              const NetworkActor writer) {
  const auto prefab = contract.replicatedPrefabs.find(std::string(prefabKey));
  if (prefab == contract.replicatedPrefabs.end())
    return nlohmann::json::object();
  const nlohmann::json full = captureReplicatedState(entity);
  nlohmann::json filtered = nlohmann::json::object();
  for (const auto &[component, fields] : full.items()) {
    for (const auto &[field, value] : fields.items()) {
      const auto rule = prefab->second.fields.find(component + "." + field);
      if (rule != prefab->second.fields.end() &&
          (writer == NetworkActor::All || rule->second.writeBy == writer ||
           rule->second.writeBy == NetworkActor::All))
        filtered[component][field] = value;
    }
  }
  return filtered;
}

ReplicatedStateResult validateReplicatedState(const nlohmann::json &state) {
  std::string error;
  return validReplicatedState(state, error)
             ? ReplicatedStateResult{.ok = true, .error = {}}
             : ReplicatedStateResult{.ok = false, .error = std::move(error)};
}

ReplicatedStateResult validateContractReplicatedState(
    const NetworkContract &contract, const std::string_view prefabKey,
    const NetworkActor writer, const nlohmann::json &state) {
  if (ReplicatedStateResult reflected = validateReplicatedState(state);
      !reflected.ok)
    return reflected;
  const auto prefab = contract.replicatedPrefabs.find(std::string(prefabKey));
  if (prefab == contract.replicatedPrefabs.end())
    return {.ok = false, .error = "unknown replicated prefab"};
  std::string error;
  for (const auto &[component, fields] : state.items())
    for (const auto &[field, value] : fields.items())
      if (!contractAllows(prefab->second, component + "." + field, writer,
                          value, error))
        return {.ok = false, .error = std::move(error)};
  return {.ok = true, .error = {}};
}

ReplicatedStateResult applyReplicatedState(Entity &entity,
                                           const nlohmann::json &state) {
  if (ReplicatedStateResult validation = validateReplicatedState(state);
      !validation.ok)
    return validation;

  if (const auto found = state.find("Transform2D");
      found != state.end() && entity.hasComponent<Transform2DComponent>()) {
    auto &transform = *entity.component<Transform2DComponent>();
    if (found->contains("position"))
      transform.position = {.x = (*found)["position"][0].get<float>(),
                            .y = (*found)["position"][1].get<float>()};
    if (found->contains("rotation"))
      transform.rotation = (*found)["rotation"].get<float>();
    if (found->contains("scale"))
      transform.scale = {.x = (*found)["scale"][0].get<float>(),
                         .y = (*found)["scale"][1].get<float>()};
  }
  if (const auto found = state.find("Transform3D");
      found != state.end() && entity.hasComponent<Transform3DComponent>()) {
    auto &transform = *entity.component<Transform3DComponent>();
    if (found->contains("position"))
      transform.position = {.x = (*found)["position"][0].get<float>(),
                            .y = (*found)["position"][1].get<float>(),
                            .z = (*found)["position"][2].get<float>()};
    if (found->contains("rotation"))
      transform.rotation = {.x = (*found)["rotation"][0].get<float>(),
                            .y = (*found)["rotation"][1].get<float>(),
                            .z = (*found)["rotation"][2].get<float>()};
    if (found->contains("scale"))
      transform.scale = {.x = (*found)["scale"][0].get<float>(),
                         .y = (*found)["scale"][1].get<float>(),
                         .z = (*found)["scale"][2].get<float>()};
  }
  if (const auto found = state.find("Rigidbody2D");
      found != state.end() && entity.hasComponent<Rigidbody2DComponent>() &&
      found->contains("velocity")) {
    entity.component<Rigidbody2DComponent>()->velocity = {
        .x = (*found)["velocity"][0].get<float>(),
        .y = (*found)["velocity"][1].get<float>()};
  }
  if (const auto found = state.find("Rigidbody3D");
      found != state.end() && entity.hasComponent<Rigidbody3DComponent>() &&
      found->contains("velocity")) {
    entity.component<Rigidbody3DComponent>()->velocity = {
        .x = (*found)["velocity"][0].get<float>(),
        .y = (*found)["velocity"][1].get<float>(),
        .z = (*found)["velocity"][2].get<float>()};
  }
  if (const auto found = state.find("Sprite");
      found != state.end() && entity.hasComponent<SpriteComponent>()) {
    auto &sprite = *entity.component<SpriteComponent>();
    if (found->contains("flip_x"))
      sprite.flipX = (*found)["flip_x"].get<bool>();
    if (found->contains("flip_y"))
      sprite.flipY = (*found)["flip_y"].get<bool>();
    if (found->contains("color")) {
      sprite.color = {.r = (*found)["color"][0].get<float>(),
                      .g = (*found)["color"][1].get<float>(),
                      .b = (*found)["color"][2].get<float>(),
                      .a = (*found)["color"][3].get<float>()};
    }
  }
  return {.ok = true, .error = {}};
}

} // namespace demi::runtime
