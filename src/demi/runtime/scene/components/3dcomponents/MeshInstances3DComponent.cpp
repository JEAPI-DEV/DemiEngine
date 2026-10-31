#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <atomic>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>
namespace demi::runtime {
namespace {

std::uint64_t nextContentRevision() {
  static std::atomic<std::uint64_t> revision{1};
  return revision.fetch_add(1, std::memory_order_relaxed);
}

bool sameTransform(const WorldTransform3D &left,
                   const WorldTransform3D &right) {
  const auto sameVector = [](Vec3 left, Vec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
  };
  return sameVector(left.position, right.position) &&
         sameVector(left.rotation, right.rotation) &&
         sameVector(left.scale, right.scale);
}

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

MeshInstances3DComponent::MeshInstances3DComponent()
    : contentRevision_(nextContentRevision()) {}

MeshInstances3DComponent &
MeshInstances3DComponent::operator=(const MeshInstances3DComponent &other) {
  if (this != &other) {
    MeshInstances3DComponent replacement(other);
    *this = std::move(replacement);
  }
  return *this;
}

MeshInstances3DComponent::MeshInstances3DComponent(
    MeshInstances3DComponent &&other) noexcept
    : transforms_(std::move(other.transforms_)),
      contentRevision_(other.contentRevision_) {
  other.transforms_.reset();
  other.contentRevision_ = nextContentRevision();
}

MeshInstances3DComponent &
MeshInstances3DComponent::operator=(MeshInstances3DComponent &&other) noexcept {
  if (this != &other) {
    transforms_ = std::move(other.transforms_);
    contentRevision_ = other.contentRevision_;
    other.transforms_.reset();
    other.contentRevision_ = nextContentRevision();
  }
  return *this;
}

MeshInstances3DComponent::Transforms &
MeshInstances3DComponent::mutableTransforms() {
  if (!transforms_) {
    transforms_ = std::make_shared<Transforms>();
  } else if (!transforms_.unique()) {
    transforms_ = std::make_shared<Transforms>(*transforms_);
  }
  return *transforms_;
}

void MeshInstances3DComponent::setTransform(std::string label,
                                            const WorldTransform3D &transform) {
  if (label.empty()) {
    throw std::invalid_argument("Mesh instance requires a nonempty label");
  }
  const auto found = transforms().find(label);
  if (found != transforms().end() && sameTransform(found->second, transform)) {
    return;
  }
  mutableTransforms().insert_or_assign(std::move(label), transform);
  contentRevision_ = nextContentRevision();
}

bool MeshInstances3DComponent::eraseTransform(const std::string &label) {
  if (!transforms().contains(label)) {
    return false;
  }
  mutableTransforms().erase(label);
  contentRevision_ = nextContentRevision();
  return true;
}

void MeshInstances3DComponent::clearTransforms() {
  if (!transforms().empty()) {
    transforms_.reset();
    contentRevision_ = nextContentRevision();
  }
}

void MeshInstances3DComponent::copyTransforms(
    MeshInstances3DComponent &destination,
    const MeshInstances3DComponent &source) {
  destination = source;
}

bool MeshInstances3DComponent::readTransforms(
    const MeshInstances3DComponent &source, nlohmann::json &out) {
  out = nlohmann::json::object();
  for (const auto &[label, transform] : source.transforms()) {
    out[label] = {
        {"position",
         {transform.position.x, transform.position.y, transform.position.z}},
        {"rotation",
         {transform.rotation.x, transform.rotation.y, transform.rotation.z}},
        {"scale", {transform.scale.x, transform.scale.y, transform.scale.z}}};
  }
  return true;
}

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
    result.setTransform(instanceId, readInstanceTransform(transform));
  }
  entity.setComponent(std::move(result));
}
} // namespace demi::runtime
