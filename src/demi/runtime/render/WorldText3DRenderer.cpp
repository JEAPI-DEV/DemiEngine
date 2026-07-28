#include "demi/runtime/render/WorldText3DRenderer.h"

#include "demi/runtime/render/Renderer3DInternal.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/WorldText3DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <raymath.h>
#include <rlgl.h>

#include <cmath>

namespace demi::runtime {
namespace {

void drawGlyphQuad(const Texture2D texture, const Rectangle source,
                   const ::Vector3 center, const ::Vector3 right,
                   const ::Vector3 up, const float width, const float height,
                   const ::Color color) {
  const ::Vector3 horizontal = Vector3Scale(right, width * 0.5F);
  const ::Vector3 vertical = Vector3Scale(up, height * 0.5F);
  const ::Vector3 bottomLeft =
      Vector3Subtract(Vector3Subtract(center, horizontal), vertical);
  const ::Vector3 bottomRight =
      Vector3Add(Vector3Subtract(center, vertical), horizontal);
  const ::Vector3 topRight =
      Vector3Add(Vector3Add(center, horizontal), vertical);
  const ::Vector3 topLeft =
      Vector3Add(Vector3Subtract(center, horizontal), vertical);
  const float u0 = source.x / static_cast<float>(texture.width);
  const float v0 = source.y / static_cast<float>(texture.height);
  const float u1 = (source.x + source.width) / static_cast<float>(texture.width);
  const float v1 =
      (source.y + source.height) / static_cast<float>(texture.height);
  rlSetTexture(texture.id);
  rlBegin(RL_QUADS);
  rlColor4ub(color.r, color.g, color.b, color.a);
  const auto vertex = [](const ::Vector3 point, const float u, const float v) {
    rlTexCoord2f(u, v);
    rlVertex3f(point.x, point.y, point.z);
  };
  vertex(bottomLeft, u0, v1);
  vertex(bottomRight, u1, v1);
  vertex(topRight, u1, v0);
  vertex(topLeft, u0, v0);
  rlEnd();
  rlSetTexture(0);
}

} // namespace

void drawWorldText3D(const World &world, const ::Camera3D &camera,
                     const Vec3 cameraPosition, const std::string &renderMask,
                     RenderStatistics &statistics) {
  const Font font = GetFontDefault();
  const ::Vector3 forward =
      Vector3Normalize(Vector3Subtract(camera.target, camera.position));
  const ::Vector3 cameraRight =
      Vector3Normalize(Vector3CrossProduct(forward, camera.up));
  for (const Entity &entity : world.entities) {
    const auto *text = entity.component<WorldText3DComponent>();
    if (!entity.enabled || text == nullptr || text->text.empty() ||
        !entity.hasComponent<Transform3DComponent>() ||
        (!renderMask.empty() && !text->renderMask.empty() &&
         renderMask != text->renderMask))
      continue;
    const Vec3 position = worldPosition3D(world, entity);
    ::Vector3 right = cameraRight;
    ::Vector3 up = Vector3Normalize(camera.up);
    if (!text->billboard) {
      if (const auto transform = resolveWorldTransform3D(world, entity)) {
        right = Vector3Normalize(renderer3d_detail::toRlVec3(
            transformDirection3D(*transform, {1.0F, 0.0F, 0.0F})));
        up = Vector3Normalize(renderer3d_detail::toRlVec3(
            transformDirection3D(*transform, {0.0F, 1.0F, 0.0F})));
      }
    }
    const float dx = position.x - cameraPosition.x;
    const float dy = position.y - cameraPosition.y;
    const float dz = position.z - cameraPosition.z;
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > text->maxDistance)
      continue;

    const Vector2 measured =
        MeasureTextEx(font, text->text.c_str(), text->fontSize, 0.0F);
    float cursor = -measured.x * 0.5F;
    for (int byte = 0; byte < static_cast<int>(text->text.size());) {
      int bytes = 0;
      const int codepoint = GetCodepointNext(text->text.c_str() + byte, &bytes);
      byte += std::max(bytes, 1);
      const int glyphIndex = GetGlyphIndex(font, codepoint);
      const GlyphInfo &glyph = font.glyphs[glyphIndex];
      const Rectangle source = font.recs[glyphIndex];
      const float advance =
          static_cast<float>(glyph.advanceX > 0 ? glyph.advanceX
                                               : static_cast<int>(source.width));
      const float worldAdvance =
          advance * text->fontSize / static_cast<float>(font.baseSize);
      const float worldHeight =
          source.height * text->fontSize / static_cast<float>(font.baseSize);
      const float worldWidth =
          source.width * text->fontSize / static_cast<float>(font.baseSize);
      const ::Vector3 glyphPosition{
          position.x + right.x * (cursor + worldAdvance * 0.5F),
          position.y + right.y * (cursor + worldAdvance * 0.5F),
          position.z + right.z * (cursor + worldAdvance * 0.5F)};
      const ::Color color = renderer3d_detail::toRlColor(text->color);
      if (text->billboard)
        DrawBillboardPro(camera, font.texture, source, glyphPosition, camera.up,
                         {worldWidth, worldHeight},
                         {worldWidth * 0.5F, worldHeight * 0.5F}, 0.0F, color);
      else
        drawGlyphQuad(font.texture, source, glyphPosition, right, up,
                      worldWidth, worldHeight, color);
      cursor += worldAdvance;
      statistics.triangles += 2;
    }
  }
}

} // namespace demi::runtime
