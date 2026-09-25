#include "demi/runtime/destruction/CosmeticDebris3D.h"
#include "demi/runtime/scene/components/3dcomponents/Destructible3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace demi::runtime;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main() {
  try {
    World world;
    Entity root;
    root.id = "wall";
    root.setComponent(Destructible3DComponent{.attachmentKey = 7});
    world.entities.push_back(root);
    Entity parsed;
    FractureDebris3DComponent::parse({{"count", 4},
                                      {"max_fragments", 6},
                                      {"lifetime", 2},
                                      {"fade_duration", 1},
                                      {"color", "#ffffff"}},
                                     parsed);
    const auto config = *parsed.component<FractureDebris3DComponent>();
    CosmeticDebris3D debris, repeat;
    debris.emit("wall", 7, config, {1, 2, 3}, {0, 1, 0});
    repeat.emit("wall", 7, config, {1, 2, 3}, {0, 1, 0});
    auto chips = debris.snapshot();
    check(chips.size() == 4 && world.entities.size() == 1,
          "Cosmetics must not create entities");
    check(chips[0].velocity.x == repeat.snapshot()[0].velocity.x,
          "Seed was not deterministic");
    check(chips[0].opacity() == 1, "Chip starts faded");
    debris.update(world, 1.5F);
    chips = debris.snapshot();
    check(chips.size() == 4 && std::abs(chips[0].opacity() - .5F) < 1e-5F,
          "Fade does not follow lifetime");
    check(chips[0].position.y != 2 &&
              chips[0].rotation.x != repeat.snapshot()[0].rotation.x,
          "Chips did not move/spin");
    debris.emit("wall", 7, config, {4, 5, 6}, {1, 0, 0});
    chips = debris.snapshot();
    check(chips.size() == 6 && chips[0].age == 1.5F && chips[2].age == 0,
          "Budget must replace oldest chips only");
    debris.update(world, 2);
    check(debris.snapshot().empty(), "Expired chips retained");
    auto zero = config;
    zero.lifetime = 0;
    debris.emit("wall", 7, zero, {}, {});
    check(debris.snapshot().empty(), "Zero lifetime emitted chips");
    zero = config;
    zero.count = 0;
    debris.emit("wall", 7, zero, {}, {});
    check(debris.snapshot().empty(), "Zero count emitted chips");
    zero = config;
    zero.maxFragments = 0;
    debris.emit("wall", 7, zero, {}, {});
    check(debris.snapshot().empty(), "Zero budget emitted chips");
    zero = config;
    zero.fadeDuration = 8;
    debris.emit("wall", 7, zero, {}, {});
    debris.update(world, 1);
    check(std::abs(debris.snapshot()[0].opacity() - .5F) < 1e-5F,
          "Long fade must cover lifetime");
    world.entities[0].component<Destructible3DComponent>()->attachmentKey = 8;
    debris.update(world, 0);
    check(debris.snapshot().empty(), "Same-ID replacement retained old chips");
    debris.emit("wall", 8, config, {}, {});
    world.entities.clear();
    debris.update(world, 0);
    check(debris.snapshot().empty(), "Owner removal retained chips");
    for (auto invalid :
         {nlohmann::json{{"count", 1.5}}, nlohmann::json{{"lifetime", -1}},
          nlohmann::json{{"size", {1, 0, 1}}}}) {
      bool rejected = false;
      try {
        FractureDebris3DComponent::parse(invalid, parsed);
      } catch (const std::exception &) {
        rejected = true;
      }
      check(rejected, "Invalid settings accepted");
    }
    std::cout << "Cosmetic debris lifecycle, budgets and parsing passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
