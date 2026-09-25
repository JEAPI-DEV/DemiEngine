#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace demi::runtime {
namespace {

Vec3 readInstanceVector(const nlohmann::json &value,
                        const std::string &fieldName) {
  const auto coordinates = value.get<std::array<float, 3>>();
  for (const float coordinate : coordinates) {
    if (!std::isfinite(coordinate)) {
      throw std::invalid_argument("Mesh instance " + fieldName +
                                  " must be finite");
    }
    if (fieldName == "scale" && coordinate == 0.0F) {
      throw std::invalid_argument("Mesh instance scale must not contain zero");
    }
  }
  return {coordinates[0], coordinates[1], coordinates[2]};
}

WorldTransform3D readInstanceTransform(const nlohmann::json &value) {
  if (!value.is_object()) {
    throw std::invalid_argument("Mesh instance transform must be an object");
  }

  WorldTransform3D transform;
  for (const auto &[fieldName, fieldValue] : value.items()) {
    if (fieldName == "position") {
      transform.position = readInstanceVector(fieldValue, fieldName);
    } else if (fieldName == "rotation") {
      transform.rotation = readInstanceVector(fieldValue, fieldName);
    } else if (fieldName == "scale") {
      transform.scale = readInstanceVector(fieldValue, fieldName);
    } else {
      throw std::invalid_argument("Unknown mesh instance transform field: " +
                                  fieldName);
    }
  }
  return transform;
}

} // namespace

nlohmann::json MeshInstances3DComponent::defaults() {
  return {{"transforms", nlohmann::json::object()}};
}

void MeshInstances3DComponent::parse(const nlohmann::json &json,
                                     Entity &entity) {
  MeshInstances3DComponent result;
  const auto transforms = json.value("transforms", nlohmann::json::object());
  if (!transforms.is_object()) {
    throw std::invalid_argument("Mesh instance transforms must be an object");
  }

  for (const auto &[instanceId, transform] : transforms.items()) {
    if (instanceId.empty()) {
      throw std::invalid_argument("Mesh instance requires a nonempty label");
    }
    result.transforms.emplace(instanceId, readInstanceTransform(transform));
  }
  entity.setComponent(std::move(result));
}
} // namespace demi::runtime
