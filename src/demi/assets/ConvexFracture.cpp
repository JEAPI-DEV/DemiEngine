#include "demi/assets/ConvexFracture.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>

namespace demi::assets {
namespace {
using P = FracturePoint;
P add(P a, P b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
P sub(P a, P b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
P mul(P a, double b) { return {a[0] * b, a[1] * b, a[2] * b}; }
double dot(P a, P b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
P cross(P a, P b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
P unit(P p) {
  const double length = std::sqrt(dot(p, p));
  if (!(length > 1e-14))
    throw std::runtime_error("Degenerate fracture face");
  return mul(p, 1 / length);
}
std::pair<P, P> bounds(const FractureSolid &s) {
  P lo{1e100, 1e100, 1e100}, hi{-1e100, -1e100, -1e100};
  for (const auto &f : s.faces)
    for (const auto &v : f.vertices)
      for (int i = 0; i < 3; ++i) {
        lo[i] = std::min(lo[i], v.position[i]);
        hi[i] = std::max(hi[i], v.position[i]);
      }
  return {lo, hi};
}
double extent(const FractureSolid &s) {
  const auto [lo, hi] = bounds(s);
  return std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
}
bool near(P a, P b, double epsilon) {
  const auto d = sub(a, b);
  return dot(d, d) <= epsilon * epsilon;
}
void uniquePush(std::vector<P> &points, P p, double epsilon) {
  if (std::ranges::none_of(points, [&](P q) { return near(p, q, epsilon); }))
    points.push_back(p);
}
std::optional<FractureSolid> clip(const FractureSolid &s, P n, double distance,
                                  double epsilon) {
  FractureSolid out{.id = s.id};
  std::vector<P> boundary;
  bool outside = false, inside = false;
  for (const auto &f : s.faces) {
    FractureFace face{.interior = f.interior};
    for (std::size_t i = 0; i < f.vertices.size(); ++i) {
      const auto &a = f.vertices[i],
                 &b = f.vertices[(i + 1) % f.vertices.size()];
      const double da = dot(n, a.position) - distance,
                   db = dot(n, b.position) - distance;
      outside |= da > epsilon;
      inside |= da < -epsilon;
      if (da <= epsilon)
        face.vertices.push_back(a);
      if ((da < -epsilon && db > epsilon) || (da > epsilon && db < -epsilon)) {
        const double t = da / (da - db);
        FractureVertex v{add(a.position, mul(sub(b.position, a.position), t)),
                         {a.uv[0] + t * (b.uv[0] - a.uv[0]),
                          a.uv[1] + t * (b.uv[1] - a.uv[1])}};
        face.vertices.push_back(v);
        uniquePush(boundary, v.position, epsilon);
      }
      if (std::abs(da) <= epsilon)
        uniquePush(boundary, a.position, epsilon);
    }
    if (face.vertices.size() >= 3)
      out.faces.push_back(std::move(face));
  }
  if (!outside)
    return s;
  if (!inside || boundary.size() < 3)
    return std::nullopt;
  P center{};
  for (auto p : boundary)
    center = add(center, p);
  center = mul(center, 1.0 / boundary.size());
  const P u = unit(cross(std::abs(n[0]) < 0.9 ? P{1, 0, 0} : P{0, 1, 0}, n)),
          v = cross(n, u);
  std::ranges::sort(boundary, [&](P a, P b) {
    a = sub(a, center);
    b = sub(b, center);
    return std::atan2(dot(a, v), dot(a, u)) < std::atan2(dot(b, v), dot(b, u));
  });
  FractureFace cap{.interior = true};
  for (auto p : boundary)
    cap.vertices.push_back({p, {dot(p, u), dot(p, v)}});
  out.faces.push_back(std::move(cap));
  return out;
}
double signedVolume(const FractureSolid &solid) {
  const P origin = solid.faces.front().vertices.front().position;
  double volume = 0;
  for (const auto &f : solid.faces)
    for (std::size_t i = 1; i + 1 < f.vertices.size(); ++i)
      volume += dot(sub(f.vertices[0].position, origin),
                    cross(sub(f.vertices[i].position, origin),
                          sub(f.vertices[i + 1].position, origin))) /
                6;
  return volume;
}
} // namespace
FracturePoint fractureNormal(const FractureFace &f) {
  for (std::size_t i = 1; i + 1 < f.vertices.size(); ++i) {
    auto n = cross(sub(f.vertices[i].position, f.vertices[0].position),
                   sub(f.vertices[i + 1].position, f.vertices[0].position));
    if (dot(n, n) > 1e-24)
      return unit(n);
  }
  throw std::runtime_error("Degenerate fracture face");
}
std::vector<FracturePoint> fracturePoints(const FractureSolid &s) {
  std::vector<P> points;
  const double epsilon = std::max(extent(s) * 1e-7, 1e-9);
  for (const auto &f : s.faces)
    for (const auto &v : f.vertices)
      uniquePush(points, v.position, epsilon);
  return points;
}
double fractureVolume(const FractureSolid &s) {
  return std::abs(signedVolume(s));
}
void validateFractureSolid(FractureSolid &s) {
  if (s.faces.size() < 4)
    throw std::runtime_error("Fracture source requires at least 4 faces");
  for (const auto &f : s.faces) {
    if (f.vertices.size() < 3)
      throw std::runtime_error("Invalid fracture polygon");
    for (const auto &v : f.vertices) {
      for (double coordinate : v.uv)
        if (!std::isfinite(coordinate))
          throw std::runtime_error("Fracture UVs must be finite");
      for (double x : v.position)
        if (!std::isfinite(x) || std::abs(x) > 1e6)
          throw std::runtime_error(
              "Fracture coordinates must be finite and within +/-1000000");
    }
  }
  const auto points = fracturePoints(s);
  if (points.size() < 4)
    throw std::runtime_error("Fracture source requires at least 4 unique vertices");
  const double e = extent(s) * 1e-6;
  if (!(e > 1e-10) || fractureVolume(s) < e * e * e)
    throw std::runtime_error("Fracture source has no solid volume");
  if (signedVolume(s) < 0)
    for (auto &f : s.faces)
      std::ranges::reverse(f.vertices);
  std::map<std::pair<std::size_t, std::size_t>, std::pair<int, int>> edges;
  for (const auto &f : s.faces) {
    const auto n = fractureNormal(f);
    for (auto p : points)
      if (dot(n, sub(p, f.vertices[0].position)) > e)
        throw std::runtime_error(
            "Fracture source is concave or has inconsistent winding; convex "
            "decomposition is not implemented");
    for (std::size_t i = 0; i < f.vertices.size(); ++i) {
      const auto locate = [&](P p) {
        return std::size_t(
            std::ranges::find_if(points, [&](P q) { return near(p, q, e); }) -
            points.begin());
      };
      auto a = locate(f.vertices[i].position),
           b = locate(f.vertices[(i + 1) % f.vertices.size()].position);
      if (a == b)
        throw std::runtime_error("Degenerate fracture edge");
      auto &edge = edges[{std::min(a, b), std::max(a, b)}];
      ++edge.first;
      edge.second += a < b ? 1 : -1;
    }
  }
  for (const auto &[edge, count] : edges)
    if (count.first != 2 || count.second != 0)
      throw std::runtime_error(
          "Fracture source must be a closed manifold solid");
}
std::vector<FractureSolid> fractureConvexSolid(FractureSolid source,
                                               std::size_t pieces,
                                               std::uint32_t seed) {
  if (pieces < 1)
    throw std::runtime_error("Fracture pieces must be positive");
  validateFractureSolid(source);
  std::vector<FractureSolid> result{std::move(source)};
  std::uint32_t random = seed ? seed : 0x9e3779b9U;
  const auto next = [&]() {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return double(random) / 4294967296.0;
  };
  while (result.size() < pieces) {
    auto largest = std::ranges::max_element(
        result, {}, [](const auto &s) { return fractureVolume(s); });
    const auto [lo, hi] = bounds(*largest);
    int axis = 0;
    for (int i = 1; i < 3; ++i)
      if (hi[i] - lo[i] > hi[axis] - lo[axis])
        axis = i;
    P center{};
    auto points = fracturePoints(*largest);
    for (auto p : points)
      center = add(center, p);
    center = mul(center, 1.0 / points.size());
    const double epsilon = extent(*largest) * 1e-8;
    std::optional<FractureSolid> left, right;
    for (int attempt = 0; attempt < 8; ++attempt) {
      P n{(next() - .5) * .5, (next() - .5) * .5, (next() - .5) * .5};
      n[axis] = 1;
      n = unit(n);
      const double d = dot(n, center);
      left = clip(*largest, n, d, epsilon);
      right = clip(*largest, mul(n, -1), -d, epsilon);
      if (left && right &&
          fractureVolume(*left) > epsilon * epsilon * epsilon &&
          fractureVolume(*right) > epsilon * epsilon * epsilon)
        break;
    }
    if (!left || !right)
      throw std::runtime_error("Could not split fracture source");
    left->id = largest->id + "0";
    right->id = largest->id + "1";
    *largest = std::move(*left);
    result.push_back(std::move(*right));
  }
  std::ranges::sort(result, {}, &FractureSolid::id);
  for (const auto &s : result)
    if (fracturePoints(s).size() > 256)
      throw std::runtime_error("Generated hull exceeds 256 vertices; increase "
                               "fracture detail or simplify the source");
  return result;
}
bool fractureSolidsTouch(const FractureSolid &a, const FractureSolid &b) {
  const double scale = std::max(extent(a), extent(b)), epsilon = scale * 1e-5;
  const auto [al, ah] = bounds(a);
  const auto [bl, bh] = bounds(b);
  for (int i = 0; i < 3; ++i)
    if (ah[i] < bl[i] - epsilon || bh[i] < al[i] - epsilon)
      return false;
  std::optional<FractureSolid> overlap = a;
  for (const auto &f : b.faces) {
    const auto n = fractureNormal(f);
    overlap = clip(*overlap, n, dot(n, f.vertices[0].position) + epsilon,
                   epsilon * .01);
    if (!overlap)
      return false;
  }
  return fractureVolume(*overlap) > 4 * epsilon * epsilon * scale;
}
} // namespace demi::assets
