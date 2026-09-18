#include "demi/runtime/destruction/DestructionCheckpoint3D.h"
#include "demi/assets/AssetHash.h"
#include "demi/runtime/physics/ColliderAsset3D.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <span>
#include <stdexcept>
namespace demi::runtime {
namespace {
using J = nlohmann::json;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::invalid_argument(message);
}
J vector(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 vector(const J &j) {
  const auto v = j.get<std::array<float, 3>>();
  for (auto n : v)
    require(std::isfinite(n) && std::abs(n) <= 1e6F,
            "Invalid checkpoint vector");
  return {v[0], v[1], v[2]};
}
} // namespace
std::string destructionGeometryHash(const ColliderAsset3D &asset) {
  J j = J::array();
  for (const auto &part : asset.parts) {
    J points = J::array();
    for (auto p : part.points)
      points.push_back(vector(p));
    j.push_back({part.id, part.density, points});
  }
  if (asset.fracture) {
    for (const auto &bond : asset.fracture->bonds)
      j.push_back({bond.id, bond.firstPart, bond.secondPart, bond.health});
    j.push_back(asset.fracture->anchors);
  }
  const auto text = j.dump();
  return assets::hashBytes(std::span(
      reinterpret_cast<const unsigned char *>(text.data()), text.size()));
}
J writeDestructionCheckpoint(const DestructionCheckpoint3D &state) {
  J groups = J::array();
  for (const auto &group : state.groups) {
    J item = {{"parts", group.parts}, {"retired", group.retired}};
    if (!group.retired) {
      item["position"] = vector(group.position);
      item["rotation"] = vector(group.rotation);
      item["velocity"] = vector(group.velocity);
      item["angular_velocity"] = vector(group.angularVelocity);
    }
    groups.push_back(std::move(item));
  }
  return {{"format_version", 1},
          {"geometry_hash", state.geometryHash},
          {"bonds", state.bonds},
          {"anchors", state.anchors},
          {"groups", groups}};
}
DestructionCheckpoint3D
readDestructionCheckpoint(const J &j, const ColliderAsset3D &asset,
                          const std::string &geometryHash) {
  require(j.is_object() && j.size() == 5 &&
              j.at("format_version").is_number() && j.at("format_version") == 1,
          "Invalid destruction checkpoint format");
  DestructionCheckpoint3D state;
  state.geometryHash = j.at("geometry_hash").get<std::string>();
  require(state.geometryHash == geometryHash,
          "Destruction checkpoint geometry/version mismatch");
  std::set<std::string> bondIds, anchorIds, partIds;
  for (const auto &part : asset.parts)
    partIds.insert(part.id);
  require(asset.fracture.has_value(), "Checkpoint requires fracture graph");
  for (const auto &bond : asset.fracture->bonds)
    bondIds.insert(bond.id);
  for (const auto &anchor : asset.fracture->anchors)
    anchorIds.insert(anchor);
  const auto damage = [&](const J &values, const auto &ids) {
    require(values.is_object() && values.size() <= ids.size(),
            "Invalid checkpoint damage map");
    std::map<std::string, float> result;
    for (const auto &[key, value] : values.items()) {
      require(ids.contains(key) && value.is_number(),
              "Unknown checkpoint damage identity");
      const auto amount = value.template get<float>();
      require(std::isfinite(amount) && amount > 0 && amount <= 1e30F,
              "Invalid checkpoint damage amount");
      result.emplace(key, amount);
    }
    return result;
  };
  state.bonds = damage(j.at("bonds"), bondIds);
  state.anchors = damage(j.at("anchors"), anchorIds);
  require(j.at("groups").is_array() && !j.at("groups").empty() &&
              j.at("groups").size() <= asset.parts.size(),
          "Invalid checkpoint groups");
  std::set<std::string> seen;
  for (const auto &item : j.at("groups")) {
    DestructionGroupCheckpoint3D group;
    group.retired = item.at("retired").get<bool>();
    require(item.size() == (group.retired ? 2 : 6),
            "Unknown checkpoint group fields");
    require(item.at("parts").is_array() && !item.at("parts").empty() &&
                item.at("parts").size() <= asset.parts.size(),
            "Invalid checkpoint group size");
    group.parts = item.at("parts").get<std::vector<std::string>>();
    for (const auto &part : group.parts)
      require(partIds.contains(part) && seen.insert(part).second,
              "Duplicate/unknown checkpoint part");
    std::ranges::sort(group.parts);
    if (!group.retired) {
      group.position = vector(item.at("position"));
      group.rotation = vector(item.at("rotation"));
      group.velocity = vector(item.at("velocity"));
      group.angularVelocity = vector(item.at("angular_velocity"));
    }
    state.groups.push_back(std::move(group));
  }
  require(seen == partIds, "Checkpoint must account for every part");
  return state;
}
} // namespace demi::runtime
