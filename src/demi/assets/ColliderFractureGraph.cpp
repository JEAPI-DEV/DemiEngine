#include "demi/assets/ColliderFractureGraph.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

namespace demi::assets {
namespace {
bool validId(const std::string &id) {
  const auto alphanumeric = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
  };
  return !id.empty() && id.size() <= 128 && alphanumeric(id.front()) &&
         std::ranges::all_of(id, [&](char c) {
           return alphanumeric(c) || c == '_' || c == '-' || c == '.';
         });
}
} // namespace

std::optional<ColliderFractureGraph>
parseColliderFractureGraph(const nlohmann::json &document,
                           std::span<const std::string> partIds,
                           std::string &error) {
  error =
      "Fracture requires bonds and optional unique anchor part IDs.";
  try {
    if (partIds.empty() || partIds.size() >= UINT32_MAX || !document.is_object() ||
        !document.contains("bonds") || !document["bonds"].is_array() ||
        document["bonds"].size() >= UINT32_MAX)
      return std::nullopt;
    for (const auto &[key, value] : document.items())
      if (key != "bonds" && key != "anchors")
        return std::nullopt;
    const std::set<std::string> parts(partIds.begin(), partIds.end());
    if (parts.size() != partIds.size())
      return std::nullopt;
    ColliderFractureGraph result;
    if (document.contains("anchors")) {
      const auto &anchors = document["anchors"];
      if (!anchors.is_array() || anchors.size() > parts.size())
        return std::nullopt;
      std::set<std::string> unique;
      for (const auto &anchor : anchors) {
        if (!anchor.is_string() || !parts.contains(anchor.get<std::string>()) ||
            !unique.insert(anchor.get<std::string>()).second) {
          error = "Fracture anchors must reference unique existing part IDs.";
          return std::nullopt;
        }
      }
      result.anchors.assign(unique.begin(), unique.end());
    }
    std::set<std::string> bondIds;
    std::set<std::pair<std::string, std::string>> edges;
    std::map<std::string, std::vector<std::string>> neighbors;
    for (const auto &entry : document["bonds"]) {
      error = "Fracture bonds require a unique ID, two distinct existing parts "
              "and positive finite health.";
      if (!entry.is_object() || !entry.contains("id") ||
          !entry["id"].is_string() || !entry.contains("parts") ||
          !entry["parts"].is_array() || entry["parts"].size() != 2 ||
          !entry["parts"][0].is_string() || !entry["parts"][1].is_string())
        return std::nullopt;
      for (const auto &[key, value] : entry.items())
        if (key != "id" && key != "parts" && key != "health")
          return std::nullopt;
      ColliderFractureBond bond{entry["id"].get<std::string>(),
                                entry["parts"][0].get<std::string>(),
                                entry["parts"][1].get<std::string>()};
      if (entry.contains("health")) {
        if (!entry["health"].is_number())
          return std::nullopt;
        bond.health = entry["health"].get<float>();
      }
      if (!validId(bond.id) || !bondIds.insert(bond.id).second ||
          !parts.contains(bond.firstPart) || !parts.contains(bond.secondPart) ||
          bond.firstPart == bond.secondPart || !std::isfinite(bond.health) ||
          bond.health <= 0)
        return std::nullopt;
      if (bond.secondPart < bond.firstPart)
        std::swap(bond.firstPart, bond.secondPart);
      if (!edges.emplace(bond.firstPart, bond.secondPart).second) {
        error = "Fracture contains duplicate bond endpoints.";
        return std::nullopt;
      }
      neighbors[bond.firstPart].push_back(bond.secondPart);
      neighbors[bond.secondPart].push_back(bond.firstPart);
      result.bonds.push_back(std::move(bond));
    }
    std::set<std::string> visited{partIds.front()};
    std::vector<std::string> queue{partIds.front()};
    for (std::size_t i = 0; i < queue.size(); ++i)
      for (const auto &next : neighbors[queue[i]])
        if (visited.insert(next).second)
          queue.push_back(next);
    if (visited.size() != parts.size()) {
      error =
          "Fracture bonds must connect every part into one initial assembly.";
      return std::nullopt;
    }
    std::ranges::sort(result.bonds, {}, &ColliderFractureBond::id);
    error.clear();
    return result;
  } catch (const nlohmann::json::exception &exception) {
    error += " ";
    error += exception.what();
    return std::nullopt;
  }
}
} // namespace demi::assets
