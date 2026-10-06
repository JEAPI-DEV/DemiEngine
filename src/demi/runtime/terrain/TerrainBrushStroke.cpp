#include "demi/runtime/terrain/TerrainBrushStroke.h"
#include <cmath>
#include <stdexcept>

namespace demi::runtime {
namespace {
using Json = nlohmann::json;

void requireEntries(const Json &entries) {
  if (!entries.is_array())
    throw std::invalid_argument("Terrain brush entries must be an array");
}

void requirePoint(const Json &point) {
  if (!point.is_array() || point.size() != 2)
    throw std::invalid_argument("Terrain brush point must be [x,z]");
  for (const auto &coordinate : point) {
    if (!coordinate.is_number() || !std::isfinite(coordinate.get<double>()) ||
        !std::isfinite(coordinate.get<float>()))
      throw std::invalid_argument("Terrain brush point must be finite Vec2");
  }
}

bool isProtection(const Json &entry) {
  return entry.contains("type") && entry.at("type").is_string() &&
         entry.at("type") == "protect";
}

void requireEntry(const Json &entry) {
  if (!entry.is_object())
    throw std::invalid_argument("Terrain brush entry must be an object");
  const bool hasCenter = entry.contains("center");
  const bool hasPoints = entry.contains("points");
  if (hasCenter && hasPoints)
    throw std::invalid_argument("Terrain brush cannot have center and points");
  if (hasCenter)
    requirePoint(entry.at("center"));
  if (hasPoints) {
    const auto &points = entry.at("points");
    if (!points.is_array() || points.empty())
      throw std::invalid_argument("Terrain brush points must be nonempty");
    if (entry.contains("snapshot") || isProtection(entry))
      throw std::invalid_argument(
          "Terrain protection snapshots require individual center stamps");
    for (const auto &point : points)
      requirePoint(point);
  }
}

void requireAppendTarget(const Json &entry) {
  if (!entry.is_object())
    throw std::invalid_argument("Terrain brush entry must be an object");
  const bool hasCenter = entry.contains("center");
  const bool hasPoints = entry.contains("points");
  if (hasCenter && hasPoints)
    throw std::invalid_argument("Terrain brush cannot have center and points");
  if (hasCenter)
    requirePoint(entry.at("center"));
  if (hasPoints) {
    const auto &points = entry.at("points");
    if (!points.is_array() || points.empty() || entry.contains("snapshot") ||
        isProtection(entry))
      throw std::invalid_argument("Invalid terrain brush points entry");
    // Earlier points were already present. Rechecking them on each appended
    // stamp would make long brush strokes quadratic.
    requirePoint(points.back());
  }
}

Json parameters(const Json &entry) {
  Json result = Json::object();
  for (auto field = entry.begin(); field != entry.end(); ++field)
    if (field.key() != "center" && field.key() != "points")
      result[field.key()] = field.value();
  return result;
}
} // namespace

void appendTerrainBrushStamp(Json &entries, Json stamp) {
  if (entries.is_null())
    entries = Json::array();
  requireEntries(entries);
  requireEntry(stamp);
  if (!stamp.contains("center"))
    throw std::invalid_argument("Terrain brush stamp requires center");
  if (entries.empty() || stamp.contains("snapshot") || isProtection(stamp)) {
    entries.push_back(std::move(stamp));
    return;
  }
  auto &previous = entries.back();
  requireAppendTarget(previous);
  if (previous.contains("snapshot") || isProtection(previous) ||
      parameters(previous).dump() != parameters(stamp).dump() ||
      (!previous.contains("center") && !previous.contains("points"))) {
    entries.push_back(std::move(stamp));
    return;
  }
  if (previous.contains("center")) {
    previous["points"] = Json::array({previous.at("center")});
    previous.erase("center");
  }
  previous["points"].push_back(std::move(stamp.at("center")));
}

Json compactTerrainBrushEntries(Json entries) {
  requireEntries(entries);
  Json compacted = Json::array();
  for (const auto &entry : entries) {
    requireEntry(entry);
    if (entry.contains("points")) {
      for (const auto &point : entry.at("points")) {
        Json stamp = parameters(entry);
        stamp["center"] = point;
        appendTerrainBrushStamp(compacted, std::move(stamp));
      }
    } else if (entry.contains("center")) {
      appendTerrainBrushStamp(compacted, entry);
    } else {
      compacted.push_back(entry); // Existing omitted-center default is valid.
    }
  }
  return compacted;
}

Json compactTerrainBrushRecipe(Json recipe) {
  if (!recipe.is_object())
    throw std::invalid_argument("Terrain recipe must be an object");
  for (const char *key : {"regions", "edits", "exclusions"})
    if (recipe.contains(key))
      recipe[key] = compactTerrainBrushEntries(std::move(recipe[key]));
  return recipe;
}

Json expandTerrainBrushEntries(const Json &entries) {
  requireEntries(entries);
  Json expanded = Json::array();
  for (const auto &entry : entries) {
    requireEntry(entry);
    if (!entry.contains("points")) {
      expanded.push_back(entry);
      continue;
    }
    for (const auto &point : entry.at("points")) {
      Json stamp = parameters(entry);
      stamp["center"] = point;
      expanded.push_back(std::move(stamp));
    }
  }
  return expanded;
}
} // namespace demi::runtime
