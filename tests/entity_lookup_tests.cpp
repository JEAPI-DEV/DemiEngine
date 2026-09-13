#include "demi/runtime/scene/EntityLookup.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace demi::runtime;
void verify(EntityLookup &lookup, std::vector<Entity> &entities) {
  for (auto &entity : entities) {
    if (lookup.find(entities, entity.id) != &entity ||
        lookup.find(std::as_const(entities), entity.id) != &entity)
      throw std::runtime_error("lookup differs from current entity storage");
  }
  if (lookup.find(entities, "missing"))
    throw std::runtime_error("missing ID resolved");
}
int main() {
  try {
    EntityLookup lookup;
    std::vector<Entity> entities;
    verify(lookup, entities);
    for (int i = 0; i < 2000; ++i) {
      Entity entity;
      entity.id = "entity_" + std::to_string(i);
      entities.push_back(std::move(entity));
    }
    verify(lookup, entities);
    entities.reserve(8000); // Reallocation must not leave cached pointers.
    verify(lookup, entities);
    std::ranges::reverse(entities);
    verify(lookup, entities);
    const auto removedId = entities[10].id;
    entities.erase(entities.begin() + 10);
    if (lookup.find(entities, removedId))
      throw std::runtime_error("erased entity resolved");
    verify(lookup, entities);
    const auto oldId = entities[20].id;
    entities[20].id = "replacement";
    if (lookup.find(entities, oldId))
      throw std::runtime_error("old ID survived rename");
    verify(lookup, entities);
    entities[20].id = "missing"; // Prior miss must not hide a newly created ID.
    if (lookup.find(entities, "missing") != &entities[20])
      throw std::runtime_error("cached miss survived rename");
    entities[20].id = "replacement";
    verify(lookup, entities);
    auto copied = entities;
    verify(lookup, copied); // Same IDs must resolve into the supplied world.
    entities.clear();
    verify(lookup, entities);
    lookup.clear();
    verify(lookup, copied);
    std::cout << "Entity lookup mutation and lifetime tests passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
