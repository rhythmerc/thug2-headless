// A THUG2 collision file (levels\<x>\<x>.col.xbx, version 10) made from triangles: what the host's world (the AR
// room and its park) becomes so the game collides with it exactly as with a level's own collision. The layout, as the
// game's loader takes it apart (WorldScene::load_collision_data, formats/collision_format.h) and as the decomp's level
// converters write it (thug2-level-converter-core level_port/thug2_col.ts):
//
//   header      i32 version 10, object count, total vertices, large faces, small faces, float vertices, fixed-point
//               vertices, 0
//   objects     64 bytes: u32 checksum, u16 flags, u16 vertex count, u16 face count, u8 small faces, u8 fixed-point
//               vertices, u32 first face (byte offset), 4f box min, 4f box max, u32 first vertex (byte offset),
//               u32 BSP root (byte offset in the nodes), u32 first intensity, f32 reach scale
//   (16-aligned) float vertices 3f; intensities, a byte a vertex; (4-aligned) faces, large: u16 flags, u16 terrain,
//   3 x u16 vertex; a reach byte a large face; (4-aligned) u32 nodes' size, the nodes (8 bytes: a split
//   {i32 axis | point in sixteenths << 2, u32 byte offset of the left child (the right one after it)} or a leaf
//   {u16 3, u16 face count, u32 first face index}), then the leaves' face indices (u16).
//
// Only large faces and float vertices are written (the plain kind). A face's reach byte is how far its farthest
// vertex is from the object's box minimum times the object's reach scale (254 / its farthest vertex), rounded up: the
// game's ray test skips a face whose reach is below the ray's distance.
#include "world/col_writer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace headless {
namespace {

constexpr int32_t kVersion = 10;
constexpr size_t kLeafFaces = 50;   // faces in a leaf before it is split (as the shipped trees)
constexpr int kMaxDepth = 40;
constexpr size_t kMaxObjectFaces = 60000; // (an object's counts are u16)

struct Bytes {
    std::vector<uint8_t> data;
    size_t tell() const { return data.size(); }
    template <class T>
    void put(T value)
    {
        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        data.insert(data.end(), p, p + sizeof(T));
    }
    template <class T>
    void patch(size_t at, T value)
    {
        std::memcpy(data.data() + at, &value, sizeof(T));
    }
    void zeros(size_t n) { data.insert(data.end(), n, 0); }
    void pad_to(size_t alignment)
    {
        while (data.size() % alignment != 0) data.push_back(0);
    }
    void append(const Bytes& other) { data.insert(data.end(), other.data.begin(), other.data.end()); }
};

struct Face {
    uint16_t flags;
    uint16_t terrain;
    std::array<uint16_t, 3> v;
};

struct Node {
    bool leaf = true;
    int axis = 0;
    int32_t point = 0; // sixteenths
    std::vector<uint16_t> faces;
    std::unique_ptr<Node> left, right;
};

struct Object {
    uint32_t checksum = 0;
    std::vector<std::array<float, 3>> verts;
    std::vector<Face> faces;
    std::array<float, 3> lo{}, hi{};
    std::unique_ptr<Node> tree;
    float reach_scale = 1.0F;
    std::vector<uint8_t> reach;
};

std::unique_ptr<Node> build(const Object& o, std::vector<uint16_t> ids, int depth, std::set<int> stuck)
{
    auto leaf = [&] {
        auto n = std::make_unique<Node>();
        n->faces = std::move(ids);
        return n;
    };
    if (ids.size() <= kLeafFaces || depth > kMaxDepth) return leaf();
    auto coord = [&](uint16_t f, int i, int axis) { return o.verts[o.faces[f].v[i]][axis]; };
    struct Best {
        size_t duplicates;
        int axis;
        int32_t point;
        std::vector<uint16_t> left, right;
    };
    std::optional<Best> best;
    for (int axis = 0; axis < 3; ++axis) {
        if (stuck.count(axis)) continue;
        std::vector<float> coords;
        for (uint16_t f : ids)
            for (int i = 0; i < 3; ++i) coords.push_back(coord(f, i, axis));
        std::sort(coords.begin(), coords.end());
        const auto point = static_cast<int32_t>(std::floor(coords[coords.size() / 2] * 16.0F));
        const float split = static_cast<float>(point) / 16.0F;
        std::vector<uint16_t> left, right;
        for (uint16_t f : ids) {
            const float a = coord(f, 0, axis), b = coord(f, 1, axis), c = coord(f, 2, axis);
            if (std::min({a, b, c}) < split) left.push_back(f);
            if (std::max({a, b, c}) >= split) right.push_back(f);
        }
        const size_t duplicates = left.size() + right.size() - ids.size();
        if (!best || duplicates < best->duplicates) best = Best{duplicates, axis, point, std::move(left), std::move(right)};
    }
    if (!best || best->duplicates >= ids.size() / 2 || best->left.empty() || best->right.empty()) return leaf();
    std::set<int> stuck_left = stuck, stuck_right = stuck;
    if (best->left.size() == ids.size()) stuck_left.insert(best->axis);
    if (best->right.size() == ids.size()) stuck_right.insert(best->axis);
    auto n = std::make_unique<Node>();
    n->leaf = false;
    n->axis = best->axis;
    n->point = best->point;
    n->left = build(o, std::move(best->left), depth + 1, stuck_left);
    n->right = build(o, std::move(best->right), depth + 1, stuck_right);
    return n;
}

// Depth first, as the shipped files: a split's two children side by side, then the left's subtree, then the right's;
// the leaves' face indices in the order of the leaves' nodes. Returns the root's byte offset in `nodes`.
size_t write_tree(const Node& root, Bytes& nodes, Bytes& indices)
{
    std::vector<std::pair<size_t, const Node*>> leaves;
    std::function<void(const Node&, size_t)> emit = [&](const Node& n, size_t at) {
        if (n.leaf) {
            leaves.emplace_back(at, &n);
            return;
        }
        const size_t children = nodes.tell();
        nodes.zeros(16);
        nodes.patch<int32_t>(at, n.point * 4 + n.axis);
        nodes.patch<uint32_t>(at + 4, static_cast<uint32_t>(children));
        emit(*n.left, children);
        emit(*n.right, children + 8);
    };
    const size_t root_at = nodes.tell();
    nodes.zeros(8);
    emit(root, root_at);
    std::sort(leaves.begin(), leaves.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [at, n] : leaves) {
        nodes.patch<int32_t>(at, static_cast<int32_t>(n->faces.size()) * 65536 + 3);
        nodes.patch<uint32_t>(at + 4, static_cast<uint32_t>(indices.tell() / 2));
        for (uint16_t f : n->faces) indices.put<uint16_t>(f);
    }
    return root_at;
}

float distance(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
}

void set_reaches(Object& o)
{
    if (o.faces.empty()) {
        o.reach_scale = 1.0F;
        return;
    }
    float farthest = 0.0F;
    for (auto& v : o.verts) farthest = std::max(farthest, distance(v, o.lo));
    if (farthest <= 0.0F) farthest = 1.0F;
    o.reach_scale = 254.0F / farthest;
    o.reach.clear();
    for (auto& f : o.faces) {
        float far = 0.0F;
        for (uint16_t i : f.v) far = std::max(far, distance(o.verts[i], o.lo));
        o.reach.push_back(static_cast<uint8_t>(std::min(255.0, std::ceil(far * o.reach_scale) + 1.0)));
    }
}

} // namespace

std::vector<uint8_t> write_collision(const std::vector<CollisionTriangle>& triangles, uint32_t first_checksum)
{
    // The triangles in objects of at most kMaxObjectFaces faces (vertices shared within an object where equal).
    std::vector<Object> objects;
    for (size_t start = 0; start < triangles.size(); start += kMaxObjectFaces) {
        Object o;
        o.checksum = first_checksum + static_cast<uint32_t>(objects.size());
        o.lo = {INFINITY, INFINITY, INFINITY};
        o.hi = {-INFINITY, -INFINITY, -INFINITY};
        std::map<std::array<float, 3>, uint16_t> index;
        const size_t end = std::min(triangles.size(), start + kMaxObjectFaces);
        for (size_t t = start; t < end; ++t) {
            const CollisionTriangle& tri = triangles[t];
            Face f{tri.flags, tri.terrain, {}};
            for (int i = 0; i < 3; ++i) {
                const std::array<float, 3> p{tri.v[i][0], tri.v[i][1], tri.v[i][2]};
                auto it = index.find(p);
                if (it == index.end()) {
                    it = index.emplace(p, static_cast<uint16_t>(o.verts.size())).first;
                    o.verts.push_back(p);
                    for (int k = 0; k < 3; ++k) {
                        o.lo[k] = std::min(o.lo[k], p[k]);
                        o.hi[k] = std::max(o.hi[k], p[k]);
                    }
                }
                f.v[i] = it->second;
            }
            o.faces.push_back(f);
        }
        std::vector<uint16_t> ids(o.faces.size());
        for (size_t i = 0; i < ids.size(); ++i) ids[i] = static_cast<uint16_t>(i);
        o.tree = build(o, std::move(ids), 0, {});
        set_reaches(o);
        objects.push_back(std::move(o));
    }

    uint32_t total_verts = 0, large_faces = 0;
    for (auto& o : objects) {
        total_verts += static_cast<uint32_t>(o.verts.size());
        large_faces += static_cast<uint32_t>(o.faces.size());
    }
    Bytes out, verts, intensities, faces, reaches, nodes, indices;
    out.put<int32_t>(kVersion);
    out.put<int32_t>(static_cast<int32_t>(objects.size()));
    out.put<int32_t>(static_cast<int32_t>(total_verts));
    out.put<int32_t>(static_cast<int32_t>(large_faces));
    out.put<int32_t>(0); // small faces
    out.put<int32_t>(static_cast<int32_t>(total_verts)); // float vertices
    out.put<int32_t>(0); // fixed-point vertices
    out.put<int32_t>(0);
    for (auto& o : objects) {
        const size_t root = write_tree(*o.tree, nodes, indices);
        out.put<uint32_t>(o.checksum);
        out.put<uint16_t>(0); // flags
        out.put<uint16_t>(static_cast<uint16_t>(o.verts.size()));
        out.put<uint16_t>(static_cast<uint16_t>(o.faces.size()));
        out.put<uint8_t>(0); // large faces
        out.put<uint8_t>(0); // float vertices
        out.put<uint32_t>(static_cast<uint32_t>(faces.tell()));
        for (int k = 0; k < 3; ++k) out.put<float>(o.lo[k]);
        out.put<float>(1.0F);
        for (int k = 0; k < 3; ++k) out.put<float>(o.hi[k]);
        out.put<float>(1.0F);
        out.put<uint32_t>(static_cast<uint32_t>(verts.tell()));
        out.put<uint32_t>(static_cast<uint32_t>(root));
        out.put<uint32_t>(static_cast<uint32_t>(intensities.tell()));
        out.put<float>(o.reach_scale);
        for (auto& v : o.verts)
            for (float c : v) verts.put<float>(c);
        for (size_t i = 0; i < o.verts.size(); ++i) intensities.put<uint8_t>(0x80);
        for (auto& f : o.faces) {
            faces.put<uint16_t>(f.flags);
            faces.put<uint16_t>(f.terrain);
            for (uint16_t i : f.v) faces.put<uint16_t>(i);
        }
        for (uint8_t r : o.reach) reaches.put<uint8_t>(r);
    }
    out.pad_to(16);
    out.append(verts);
    out.append(intensities);
    out.pad_to(4);
    out.append(faces);
    out.append(reaches);
    out.pad_to(4);
    out.put<uint32_t>(static_cast<uint32_t>(nodes.tell()));
    out.append(nodes);
    out.append(indices);
    return out.data;
}

} // namespace headless
