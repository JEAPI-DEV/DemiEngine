#include "demi/runtime/render/backend/BgfxVertexLayout.h"

#include <unordered_set>

namespace demi::runtime::render {
namespace {

bgfx::Attrib::Enum semantic(const VertexSemantic value) {
  switch (value) {
  case VertexSemantic::Position: return bgfx::Attrib::Position;
  case VertexSemantic::Normal: return bgfx::Attrib::Normal;
  case VertexSemantic::Tangent: return bgfx::Attrib::Tangent;
  case VertexSemantic::Bitangent: return bgfx::Attrib::Bitangent;
  case VertexSemantic::Color0: return bgfx::Attrib::Color0;
  case VertexSemantic::Color1: return bgfx::Attrib::Color1;
  case VertexSemantic::TexCoord0: return bgfx::Attrib::TexCoord0;
  case VertexSemantic::TexCoord1: return bgfx::Attrib::TexCoord1;
  case VertexSemantic::TexCoord2: return bgfx::Attrib::TexCoord2;
  case VertexSemantic::TexCoord3: return bgfx::Attrib::TexCoord3;
  case VertexSemantic::Indices: return bgfx::Attrib::Indices;
  case VertexSemantic::Weight: return bgfx::Attrib::Weight;
  }
  return bgfx::Attrib::Position;
}

bgfx::AttribType::Enum elementType(const VertexElementType type) {
  switch (type) {
  case VertexElementType::UInt8: return bgfx::AttribType::Uint8;
  case VertexElementType::Int16: return bgfx::AttribType::Int16;
  case VertexElementType::Half: return bgfx::AttribType::Half;
  case VertexElementType::Float: return bgfx::AttribType::Float;
  }
  return bgfx::AttribType::Float;
}

} // namespace

bool buildBgfxVertexLayout(const VertexLayout &source,
                           bgfx::VertexLayout &result, std::string &error) {
  if (source.attributes.empty()) {
    error = "Vertex buffers require at least one vertex attribute.";
    return false;
  }
  std::unordered_set<VertexSemantic> semantics;
  result.begin();
  for (const VertexAttribute &attribute : source.attributes) {
    if (attribute.components < 1 || attribute.components > 4) {
      error = "Vertex attribute component counts must be between 1 and 4.";
      return false;
    }
    if (!semantics.insert(attribute.semantic).second) {
      error = "Vertex layouts cannot contain duplicate semantics.";
      return false;
    }
    result.add(semantic(attribute.semantic), attribute.components,
               elementType(attribute.type), attribute.normalized,
               attribute.asInteger);
  }
  result.end();
  if (result.getStride() == 0) {
    error = "Vertex layout has an invalid zero-byte stride.";
    return false;
  }
  return true;
}

} // namespace demi::runtime::render
