#include "demi/runtime/network/GameNetworkSession.h"
#include "demi/runtime/network/ReplicatedState.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/Entity.h"

#include <nlohmann/json.hpp>

#include <iostream>

namespace {

bool testAuthority() {
  demi::runtime::GameNetworkSession host;
  host.reset(true);
  host.peerConnected(1);
  host.peerConnected(2);
  if (host.diagnostics().connectedPeers != 2 ||
      !host.registerEntity("player_1", "peer1") ||
      !host.acceptsStateFrom("player_1", "peer1") ||
      host.acceptsStateFrom("player_1", "peer2")) {
    std::cerr << "Authority registration did not enforce ownership.\n";
    return false;
  }
  if (host.registerEntity("player_1", "peer2") ||
      host.diagnostics().rejectedMessages != 1) {
    std::cerr << "Conflicting entity ownership was not rejected.\n";
    return false;
  }
  if (!host.setOwner("player_1", "peer2") ||
      !host.acceptsStateFrom("player_1", "peer2")) {
    std::cerr << "Host authority transfer failed.\n";
    return false;
  }
  host.peerDisconnected(2);
  if (host.owner("player_1").has_value() ||
      host.diagnostics().connectedPeers != 1) {
    std::cerr << "Disconnected peer ownership was not released.\n";
    return false;
  }
  return true;
}

bool testReplicatedStateAllowList() {
  demi::runtime::Entity source;
  source.id = "player";
  source.setComponent(demi::runtime::Transform2DComponent{
      .position = {.x = 3.0F, .y = 4.0F},
      .rotation = 0.5F,
      .scale = {.x = 2.0F, .y = 2.0F},
  });
  source.setComponent(demi::runtime::Rigidbody2DComponent{
      .velocity = {.x = 5.0F, .y = -1.0F},
  });
  source.setComponent(demi::runtime::SpriteComponent{
      .texture = "asset://private/server-only-texture",
      .color = {.r = 0.1F, .g = 0.2F, .b = 0.3F, .a = 1.0F},
  });

  const nlohmann::json state = demi::runtime::captureReplicatedState(source);
  if (state["Transform2D"].contains("parent") ||
      state["Sprite"].contains("texture") ||
      !demi::runtime::isReplicatedField("Transform2D", "position") ||
      demi::runtime::isReplicatedField("Sprite", "texture")) {
    std::cerr << "Replicated state leaked a field outside the allow-list.\n";
    return false;
  }

  demi::runtime::Entity target;
  target.id = "ghost";
  target.setComponent(demi::runtime::Transform2DComponent{});
  target.setComponent(demi::runtime::Rigidbody2DComponent{});
  target.setComponent(demi::runtime::SpriteComponent{
      .texture = "asset://sprites/ghost",
  });
  const auto applied = demi::runtime::applyReplicatedState(target, state);
  if (!applied.ok ||
      target.component<demi::runtime::Transform2DComponent>()->position.x !=
          3.0F ||
      target.component<demi::runtime::SpriteComponent>()->texture !=
          "asset://sprites/ghost") {
    std::cerr << "Allowed replicated state did not apply correctly.\n";
    return false;
  }

  nlohmann::json malicious = state;
  malicious["Sprite"]["texture"] = "asset://untrusted/override";
  const auto rejected =
      demi::runtime::applyReplicatedState(target, malicious);
  if (rejected.ok || rejected.error.find("not allowed") == std::string::npos) {
    std::cerr << "Unmarked replicated field was not rejected.\n";
    return false;
  }
  return true;
}

} // namespace

int main() {
  return testAuthority() && testReplicatedStateAllowList() ? 0 : 1;
}
