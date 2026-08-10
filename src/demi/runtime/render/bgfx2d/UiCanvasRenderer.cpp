#include "demi/runtime/render/bgfx2d/UiCanvasRenderer.h"

#include "demi/runtime/ui/TextEditingEngine.h"
#include "demi/runtime/ui/TextLayoutEngine.h"
#include "demi/runtime/ui/UiPresentation.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace demi::runtime::render {
namespace {

Rect2D scaledRect(const ui::UiNode &node, const float scaleX,
                  const float scaleY) {
  const float width = node.resolved.width * scaleX * node.scale;
  const float height = node.resolved.height * scaleY * node.scale;
  const float centerX = (node.resolved.x + node.resolved.width * 0.5F) * scaleX;
  const float centerY = (node.resolved.y + node.resolved.height * 0.5F) * scaleY;
  return {.x = centerX - width * 0.5F,
          .y = centerY - height * 0.5F,
          .width = width,
          .height = height};
}

ScissorRect intersect(const ScissorRect left, const ScissorRect right) {
  if (left.width == 0 || left.height == 0)
    return right;
  if (right.width == 0 || right.height == 0)
    return left;
  const std::uint32_t x0 = std::max(left.x, right.x);
  const std::uint32_t y0 = std::max(left.y, right.y);
  const std::uint32_t x1 =
      std::min<std::uint32_t>(left.x + left.width, right.x + right.width);
  const std::uint32_t y1 =
      std::min<std::uint32_t>(left.y + left.height, right.y + right.height);
  if (x1 <= x0 || y1 <= y0)
    return {.x = static_cast<std::uint16_t>(x0),
            .y = static_cast<std::uint16_t>(y0),
            .width = 1,
            .height = 1};
  return {.x = static_cast<std::uint16_t>(x0),
          .y = static_cast<std::uint16_t>(y0),
          .width = static_cast<std::uint16_t>(x1 - x0),
          .height = static_cast<std::uint16_t>(y1 - y0)};
}

ScissorRect toScissor(const Rect2D &rect) {
  const float x0 = std::max(std::floor(rect.x), 0.0F);
  const float y0 = std::max(std::floor(rect.y), 0.0F);
  const float x1 = std::max(std::ceil(rect.x + rect.width), x0 + 1.0F);
  const float y1 = std::max(std::ceil(rect.y + rect.height), y0 + 1.0F);
  return {.x = static_cast<std::uint16_t>(
              std::min(x0, static_cast<float>(UINT16_MAX))),
          .y = static_cast<std::uint16_t>(
              std::min(y0, static_cast<float>(UINT16_MAX))),
          .width = static_cast<std::uint16_t>(
              std::min(x1 - x0, static_cast<float>(UINT16_MAX))),
          .height = static_cast<std::uint16_t>(
              std::min(y1 - y0, static_cast<float>(UINT16_MAX)))};
}

Color buttonFill(const ui::UiNode &node) {
  Color fill =
      node.hovered
          ? (node.hoverColor.a > 0.0F ? node.hoverColor : node.backgroundColor)
          : node.backgroundColor;
  if (node.disabled) {
    fill.r *= 0.4F;
    fill.g *= 0.4F;
    fill.b *= 0.4F;
  }
  return fill;
}

} // namespace

Rect2D uiTextBounds(Rect2D authored, const float measuredWidth,
                    const float measuredHeight) {
  if (authored.width <= 0.0F)
    authored.width = std::max(measuredWidth, 1.0F);
  if (authored.height <= 0.0F)
    authored.height = std::max(measuredHeight, 1.0F);
  return authored;
}

bool uiCaretVisible(const float animationTime) {
  constexpr float BlinkPeriod = 1.0F;
  constexpr float VisibleDuration = 0.55F;
  if (!std::isfinite(animationTime) || animationTime < 0.0F)
    return true;
  return std::fmod(animationTime, BlinkPeriod) < VisibleDuration;
}

UiCanvasRenderer::UiCanvasRenderer(
    Canvas2D &canvas, FontAtlas2D &font, UiTextureLookup textureLookup,
    const std::unordered_map<std::string, TextureAnimation2D> *animations,
    const float animationTime)
    : canvas_(canvas), font_(font), textureLookup_(std::move(textureLookup)),
      animations_(animations), animationTime_(animationTime) {}

bool UiCanvasRenderer::draw(const ui::UiDocument &document,
                            const std::uint16_t viewportWidth,
                            const std::uint16_t viewportHeight) {
  const float scaleX = viewportWidth / std::max(document.canvasSize.x, 1.0F);
  const float scaleY = viewportHeight / std::max(document.canvasSize.y, 1.0F);
  locale_ = document.locale;
  for (const ui::UiPresentationNode &presented :
       ui::buildUiPresentation(document)) {
    if (presented.visible &&
        !drawNode(*presented.node, scaleX, scaleY,
                  clipFor(document, *presented.node, scaleX, scaleY),
                  document.focusedId == presented.node->id))
      return false;
  }
  return true;
}

ScissorRect UiCanvasRenderer::clipFor(const ui::UiDocument &document,
                                      const ui::UiNode &node,
                                      const float scaleX,
                                      const float scaleY) const {
  ScissorRect result;
  std::string parentId = node.parent;
  while (!parentId.empty()) {
    const auto parent =
        std::ranges::find(document.nodes, parentId, &ui::UiNode::id);
    if (parent == document.nodes.end())
      break;
    if (parent->type == "scroll")
      result =
          intersect(result, toScissor(scaledRect(*parent, scaleX, scaleY)));
    parentId = parent->parent;
  }
  return result;
}

bool UiCanvasRenderer::drawNode(const ui::UiNode &node, const float scaleX,
                                const float scaleY, const ScissorRect scissor,
                                const bool focused) {
  const Rect2D rect = scaledRect(node, scaleX, scaleY);
  const float scale = std::min(scaleX, scaleY) * node.scale;
  const auto solid = [&](const Color &color) {
    return color.a <= 0.0F || canvas_.solid(rect, packVertexColorRgba8(color),
                                            BlendMode::Alpha, scissor);
  };
  const bool panel = node.type == "panel" || node.type == "container" ||
                     node.type == "scroll" || node.type == "list" ||
                     node.type == "modal";
  const bool button = node.type == "button" || node.type == "toggle" ||
                      node.type == "text_input" ||
                      node.type == "virtual_button" ||
                      node.type == "virtual_stick";

  if (panel) {
    if (!solid(ui::uiPanelFillColor(node)))
      return false;
  } else if (button) {
    if (!solid(buttonFill(node)))
      return false;
  } else if (node.type == "rect") {
    if (!solid(node.backgroundColor))
      return false;
  } else if (node.type == "circle") {
    if (!canvas_.circle(rect.x + rect.width * 0.5F, rect.y + rect.height * 0.5F,
                        node.radius * scale, packVertexColorRgba8(node.color),
                        32, BlendMode::Alpha, scissor))
      return false;
  } else if (node.type == "progress" || node.type == "slider") {
    if (!solid(node.backgroundColor))
      return false;
    const float range = std::max(node.maximum - node.minimum, 0.0001F);
    const float fraction =
        std::clamp((node.value - node.minimum) / range, 0.0F, 1.0F);
    if (fraction > 0.0F && !canvas_.solid(Rect2D{.x = rect.x,
                                                 .y = rect.y,
                                                 .width = rect.width * fraction,
                                                 .height = rect.height},
                                          packVertexColorRgba8(node.color),
                                          BlendMode::Alpha, scissor))
      return false;
  } else if (node.type == "image" && textureLookup_) {
    const TextureView2D texture = imageTexture(node);
    if (texture.handle && texture.width > 0 && texture.height > 0) {
      const float sourceWidth =
          node.sourceSize.x > 0.0F ? node.sourceSize.x : texture.width;
      const float sourceHeight =
          node.sourceSize.y > 0.0F ? node.sourceSize.y : texture.height;
      if (!canvas_.image(
              texture.handle, rect,
              TextureRegion2D{
                  .u0 = node.sourcePosition.x / texture.width,
                  .v0 = node.sourcePosition.y / texture.height,
                  .u1 = (node.sourcePosition.x + sourceWidth) / texture.width,
                  .v1 = (node.sourcePosition.y + sourceHeight) / texture.height,
              },
              packVertexColorRgba8(node.color), BlendMode::Alpha, scissor))
        return false;
    }
  }

  if (node.borderWidth > 0.0F && node.borderColor.a > 0.0F) {
    const float border = node.borderWidth * scale;
    const std::uint32_t color = packVertexColorRgba8(node.borderColor);
    if (!canvas_.solid({rect.x, rect.y, rect.width, border}, color,
                       BlendMode::Alpha, scissor) ||
        !canvas_.solid(
            {rect.x, rect.y + rect.height - border, rect.width, border}, color,
            BlendMode::Alpha, scissor) ||
        !canvas_.solid({rect.x, rect.y, border, rect.height}, color,
                       BlendMode::Alpha, scissor) ||
        !canvas_.solid(
            {rect.x + rect.width - border, rect.y, border, rect.height}, color,
            BlendMode::Alpha, scissor))
      return false;
  }

  const bool textNode = node.type == "label" || node.type == "text" || button;
  const bool textInput = node.type == "text_input";
  const std::string displayedText =
      textInput ? ui::TextEditingEngine::displayText(node.text, node.textEdit)
                : node.text;
  if (textNode && (!displayedText.empty() || (textInput && focused))) {
    const float authored = node.fontSize > 0.0F ? node.fontSize : 20.0F;
    const float fontScale = authored * scale / 48.0F;
    const bool centeredControl = button && !textInput;
    ui::TextLayoutRequest request{.text = displayedText,
                                  .width = rect.width,
                                  .height = rect.height,
                                  .fontSize = authored * scale,
                                  .lineSpacing = node.lineSpacing * scale,
                                  .wrap = node.textWrap == ui::TextWrapMode::Word
                                              ? ui::TextWrap::Word
                                          : node.textWrap == ui::TextWrapMode::Grapheme
                                              ? ui::TextWrap::Grapheme
                                              : ui::TextWrap::None,
                                  .horizontal = (centeredControl || node.textHorizontalAlignment == ui::Alignment::Center)
                                                    ? ui::TextHorizontalAlignment::Center
                                                : node.textHorizontalAlignment == ui::Alignment::End
                                                    ? ui::TextHorizontalAlignment::End
                                                    : ui::TextHorizontalAlignment::Start,
                                  .vertical = (centeredControl || node.textVerticalAlignment == ui::Alignment::Center)
                                                  ? ui::TextVerticalAlignment::Center
                                              : node.textVerticalAlignment == ui::Alignment::End
                                                  ? ui::TextVerticalAlignment::End
                                                  : ui::TextVerticalAlignment::Start,
                                  .overflow = node.textOverflow == ui::TextOverflowMode::Ellipsis
                                                  ? ui::TextOverflow::Ellipsis
                                              : node.textOverflow == ui::TextOverflowMode::Visible
                                                  ? ui::TextOverflow::Visible
                                                  : ui::TextOverflow::Clip,
                                  .maxLines = node.maxLines,
                                  .locale = std::string(locale_),
                                  .fontRevision = font_.fonts().revision()};
    const ui::TextLayoutResult layout = ui::TextLayoutEngine{}.layout(
        request, [&](const std::string_view value) {
          return font_.measure(value, fontScale).width;
        }, [&](const std::string_view value) {
          return font_.shape(value, fontScale, request.direction,
                             request.locale);
        });
    const Rect2D textBounds =
        uiTextBounds(rect, layout.width, layout.height);
    const Color color = node.textColor.a > 0.0F ? node.textColor : node.color;
    const ScissorRect textScissor =
        node.textOverflow == ui::TextOverflowMode::Visible
            ? scissor
            : intersect(scissor, toScissor(textBounds));

    if (textInput && focused) {
      ui::TextEditRange selected =
          ui::TextEditingEngine::selection(node.textEdit);
      std::size_t selectionFirst = selected.first;
      std::size_t selectionCount = selected.count;
      if (!node.textEdit.composition.empty()) {
        selectionFirst += node.textEdit.compositionSelectionStart;
        selectionCount = node.textEdit.compositionSelectionLength;
      }
      for (const ui::Rect &selectionRect :
           ui::TextLayoutEngine::selectionRects(
               layout, selectionFirst, selectionCount)) {
        if (!canvas_.solid(
                {.x = rect.x + selectionRect.x,
                 .y = rect.y + selectionRect.y,
                 .width = selectionRect.width,
                 .height = selectionRect.height},
                packVertexColorRgba8(
                    {.r = 0.2F, .g = 0.45F, .b = 0.9F, .a = 0.4F}),
                BlendMode::Alpha, textScissor))
          return false;
      }
    }
    for (const auto &line : layout.lines)
      if (!font_.draw(canvas_, line.shaped, rect.x + line.x,
                      rect.y + line.y + authored * scale,
                      packVertexColorRgba8(color), 1.0F, textScissor))
        return false;
    if (textInput && focused) {
      if (!node.textEdit.composition.empty()) {
        const ui::TextEditRange selected =
            ui::TextEditingEngine::selection(node.textEdit);
        const std::size_t compositionCount =
            ui::TextLayoutEngine::graphemeCount(node.textEdit.composition);
        for (const ui::Rect &compositionRect :
             ui::TextLayoutEngine::selectionRects(
                 layout, selected.first, compositionCount)) {
          if (!canvas_.solid(
                  {.x = rect.x + compositionRect.x,
                   .y = rect.y + compositionRect.y +
                        compositionRect.height - std::max(scale, 1.0F),
                   .width = std::max(compositionRect.width, 1.0F),
                   .height = std::max(scale, 1.0F)},
                  packVertexColorRgba8(color), BlendMode::Alpha,
                  textScissor))
            return false;
        }
      }
      const std::size_t caret =
          ui::TextEditingEngine::displayCaret(node.text, node.textEdit);
      const auto caretGeometry = std::ranges::find(
          layout.carets, caret, &ui::TextLayoutResult::Caret::grapheme);
      if (caretGeometry != layout.carets.end() &&
          uiCaretVisible(animationTime_) &&
          !canvas_.solid(
              {.x = rect.x + caretGeometry->x,
               .y = rect.y + caretGeometry->y,
               .width = std::max(scale, 1.0F),
               .height = caretGeometry->height},
              packVertexColorRgba8(color), BlendMode::Alpha, textScissor))
        return false;
    }
  }
  return true;
}

TextureView2D UiCanvasRenderer::imageTexture(const ui::UiNode &node) const {
  std::string textureId = node.texture;
  if (animations_ != nullptr && !node.animation.empty()) {
    const auto animation = animations_->find(node.animation);
    if (animation == animations_->end() || animation->second.frameCount == 0)
      return {};
    textureId = node.animation + "#" +
                std::to_string(static_cast<std::size_t>(node.animationFrame) %
                               animation->second.frameCount);
  } else if (animations_ != nullptr) {
    const auto animation = animations_->find(node.texture);
    if (animation != animations_->end() &&
        !animation->second.frameDurations.empty()) {
      textureId = node.texture + "#" +
                  std::to_string(textureAnimationFrameAt(animation->second,
                                                         animationTime_));
    }
  }
  return textureLookup_(textureId);
}

} // namespace demi::runtime::render
