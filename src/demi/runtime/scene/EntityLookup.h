#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {
struct Entity;

// Thread-confined lookup accelerator for an owner such as a Lua VM. Entity IDs
// must be unique, as required by the scene/runtime object contract. Stores no
// entity pointers and validates every hit against the current collection.
class EntityLookup {
public:
  Entity *find(std::vector<Entity> &entities, const std::string &id);
  const Entity *find(const std::vector<Entity> &entities,
                     const std::string &id);
  void clear();

private:
  void rebuild(const std::vector<Entity> &entities);
  std::optional<std::size_t> index(const std::vector<Entity> &entities,
                                   const std::string &id);
  std::unordered_map<std::string, std::size_t> indices_;
  std::size_t size_ = 0;
};
} // namespace demi::runtime
