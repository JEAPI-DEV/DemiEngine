#include "demi/runtime/render/backend/QuadBatch.h"

#include <cassert>

using namespace demi::runtime::render;

int main() {
  QuadBatch disabled(0);
  assert(!disabled.add({}, {}));
  assert(disabled.draws().empty());

  QuadBatch batch(2);
  const QuadBatchKey alpha{
      .texture = TextureHandle{.index = 1, .generation = 1},
      .program = ProgramHandle{.index = 2, .generation = 1},
      .blend = BlendMode::Alpha};
  const QuadBatchKey additive{
      .texture = alpha.texture,
      .program = alpha.program,
      .blend = BlendMode::Additive};
  const Quad2D first{.left = 10.0F,
                     .top = 20.0F,
                     .right = 30.0F,
                     .bottom = 40.0F,
                     .u0 = 0.25F,
                     .v0 = 0.5F,
                     .u1 = 0.75F,
                     .v1 = 1.0F,
                     .rgba = 0x11223344U};
  assert(batch.add(alpha, first));
  assert(batch.add(alpha, {}));
  assert(batch.draws().size() == 1);
  assert(batch.draws()[0].firstIndex == 0);
  assert(batch.draws()[0].vertexCount == 8);
  assert(batch.draws()[0].indexCount == 12);

  // Capacity and render-state changes both force independent draw ranges.
  assert(batch.add(alpha, {}));
  assert(batch.add(additive, {}));
  assert(batch.draws().size() == 3);
  assert(batch.draws()[1].firstIndex == 12);
  assert(batch.draws()[1].firstVertex == 8);
  assert(batch.draws()[1].indexCount == 6);
  assert(batch.draws()[1].vertexCount == 4);
  assert(batch.draws()[2].firstIndex == 18);
  assert(batch.draws()[2].firstVertex == 12);
  assert(batch.draws()[2].indexCount == 6);

  assert(batch.quadCount() == 4);
  assert(batch.vertices().size() == 16);
  assert(batch.indices().size() == 24);
  assert(batch.vertices()[0].x == 10.0F);
  assert(batch.vertices()[0].u == 0.25F);
  assert(batch.vertices()[2].y == 40.0F);
  assert(batch.vertices()[2].rgba == 0x11223344U);
  assert(batch.indices()[0] == 0);
  assert(batch.indices()[6] == 4);
  assert(batch.indices()[12] == 0);

  batch.clear();
  const Triangle2D triangle{
      .a = {.x = 1.0F}, .b = {.x = 2.0F}, .c = {.x = 3.0F}};
  assert(batch.addTriangle(alpha, triangle));
  assert(batch.draws()[0].vertexCount == 3);
  assert(batch.draws()[0].indexCount == 3);
  assert(batch.indices()[2] == 2);
  batch.clear();
  assert(batch.addQuad(alpha,
                       {.topLeft = {.x = 1},
                        .topRight = {.x = 2},
                        .bottomRight = {.x = 3},
                        .bottomLeft = {.x = 4}}));
  assert(batch.vertices()[3].x == 4);

  batch.clear();
  assert(batch.quadCount() == 0);
  assert(batch.vertices().empty());
  assert(batch.indices().empty());
  assert(batch.draws().empty());
  return 0;
}
