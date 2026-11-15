// The skater's look for a host that draws it (app/thug2_engine.h): its skinned meshes in their bind pose, the textures
// they draw with, and each frame's pose - read from what the game built for its own (PC) renderer, which draws
// nothing here. The pose is there because the loop poses skeletons without drawing (LoopPace::pose_without_draw).
//
// Skater -> its model module (MeshInstanceModule) -> RenderModel's geoms -> each PcGeom's instance -> its mesh list
// (SceneRef) -> MeshRef: the CPU copy of a skinned mesh's vertices (SkinVertex, 0x4C bytes: what the renderer skins
// from), its index strips (level of detail 0), its material's first pass's texture (whose texels the in-memory GPU
// keeps). The pose: the skeleton module's rig's matrices (the skin palette) and the model module's draw matrix.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "app/thug2_engine.h"
#include "backends/backends.h"
#include "engine/gfx/model.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/surface_material.h"
#include "engine/objects/entity.h"
#include "engine/objects/model_component.h"
#include "engine/objects/skeleton_component.h"
#include "render_pc/bitmap_texture.h"
#include "render_pc/geom.h"
#include "render_pc/mesh_list.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/vertex_buffers.h"

namespace {

constexpr uint32_t kSkater = 0x5B8AB877;       // Skater (the local skater's alias)
constexpr uint32_t kModelModule = 0x286A8D26;  // its model (objects::MeshInstanceModule)
constexpr uint32_t kSkeletonModule = 0x222756D5; // its skeleton (objects::BoneRigModule)
constexpr uint32_t kTriangleStrip = 5;
constexpr uint32_t kBlendMask = 0xFFFFFF;

// The renderer's copy of a skinned vertex (render_pc/pc_mesh_render.cpp's SkinVertex).
struct SkinVertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    uint32_t color;
    std::array<float, 8> uv; // (u, v) a set
    std::array<uint8_t, 4> bones;
    std::array<float, 3> weights;
};
static_assert(sizeof(SkinVertex) == 0x4C);

objects::GameEntity* skater() { return static_cast<objects::GameEntity*>(objects::find_object(kSkater)); }

objects::MeshInstanceModule* model_module(objects::GameEntity* entity)
{
    return entity ? static_cast<objects::MeshInstanceModule*>(entity->fetch_module(kModelModule)) : nullptr;
}

// The skater's skinned meshes, as the renderer would draw them (shown geoms, the meshes with a skinning copy).
std::vector<render::MeshRef*> skater_meshes()
{
    std::vector<render::MeshRef*> out;
    objects::MeshInstanceModule* module = model_module(skater());
    if (!module || !module->mesh_instance()) return out;
    render::RenderModel& model = *module->mesh_instance();
    const auto* head = model.geom_ring().head();
    if (!head) return out;
    for (auto* node = head->next; node && node != head; node = node->next) {
        const render::ModelGeom* entry = node->value;
        if (!entry || !entry->geom || entry->geom->kind() != render::GeomKind::kPc) continue;
        const auto* geom = static_cast<const render::PcGeom*>(entry->geom);
        const render::Singleton* instance = geom->instance();
        if (!instance || !instance->active() || !instance->scene()) continue;
        for (render::MeshRef* mesh : instance->scene()->mesh_run()) {
            if (mesh && mesh->per_vertex_data() && mesh->vertex_count() > 0 && mesh->index_store(0)) out.push_back(mesh);
        }
    }
    return out;
}

// Level 0's indices as a triangle list (a strip's degenerate triangles dropped, every other one turned round).
std::vector<uint16_t> triangle_list(render::MeshRef* mesh)
{
    std::vector<uint16_t> out;
    const render::IdxStore* store = mesh->index_store(0);
    const uint32_t count = std::min<uint32_t>(mesh->index_counts()[0], static_cast<uint32_t>(store->index_run().size()));
    const uint16_t* in = store->indices();
    if (mesh->primitive_type() != kTriangleStrip) {
        out.assign(in, in + count - count % 3);
        return out;
    }
    for (uint32_t i = 2; i < count; ++i) {
        const uint16_t a = in[i - 2], b = in[i - 1], c = in[i];
        if (a == b || b == c || a == c) continue;
        if (i % 2 == 0) {
            out.insert(out.end(), {a, b, c});
        } else {
            out.insert(out.end(), {b, a, c});
        }
    }
    return out;
}

const render::MaterialRef* material_of(const render::MeshRef* mesh) { return mesh->material(); }

render::TextureImage* texture_of(const render::MeshRef* mesh)
{
    const render::MaterialRef* material = material_of(mesh);
    return (material && material->pass_count() > 0) ? material->textures()[0] : nullptr;
}

// The textures the meshes draw with, each once (thug2_skater_meshes fills it; thug2_skater_texture reads it).
std::vector<render::TextureImage*>& textures()
{
    static std::vector<render::TextureImage*> list;
    return list;
}

uint32_t format_of(uint32_t gpu_format)
{
    constexpr uint32_t kDxt1 = '1' << 24 | 'T' << 16 | 'X' << 8 | 'D';
    constexpr uint32_t kDxt5 = '5' << 24 | 'T' << 16 | 'X' << 8 | 'D';
    constexpr uint32_t kA8R8G8B8 = 21;
    constexpr uint32_t kX8R8G8B8 = 22;
    switch (gpu_format) {
    case kDxt1: return THUG2_TEXTURE_DXT1;
    case kDxt5: return THUG2_TEXTURE_DXT5;
    case kA8R8G8B8:
    case kX8R8G8B8: return THUG2_TEXTURE_BGRA8;
    default: return 0;
    }
}

} // namespace

extern "C" uint32_t thug2_skater_meshes(Thug2Mesh* out, uint32_t max)
{
    const std::vector<render::MeshRef*> meshes = skater_meshes();
    std::vector<render::TextureImage*>& list = textures();
    list.clear();
    for (uint32_t i = 0; i < meshes.size(); ++i) {
        render::MeshRef* mesh = meshes[i];
        int32_t texture = -1;
        if (render::TextureImage* image = texture_of(mesh); image && image->texture) {
            auto at = std::find(list.begin(), list.end(), image);
            if (at == list.end()) at = list.insert(list.end(), image);
            texture = static_cast<int32_t>(at - list.begin());
        }
        if (!out || i >= max) continue;
        Thug2Mesh& m = out[i];
        m = {};
        m.vertex_count = mesh->vertex_count();
        m.index_count = static_cast<uint32_t>(triangle_list(mesh).size());
        m.texture = texture;
        if (const render::MaterialRef* material = material_of(mesh)) {
            m.two_sided = material->two_sided();
            m.alpha_ref = material->alpha_ref();
            m.blended = (material->blend_modes()[0] & kBlendMask) != 0 ? 1 : 0;
            std::memcpy(m.color, material->pass_color()[0].data(), sizeof(m.color));
        } else {
            std::fill(std::begin(m.color), std::end(m.color), 0.5F);
        }
    }
    return static_cast<uint32_t>(meshes.size());
}

extern "C" int thug2_skater_mesh_data(uint32_t index, Thug2Vertex* vertices, uint16_t* indices)
{
    const std::vector<render::MeshRef*> meshes = skater_meshes();
    if (index >= meshes.size()) return 1;
    render::MeshRef* mesh = meshes[index];
    const auto* in = reinterpret_cast<const SkinVertex*>(mesh->per_vertex_data());
    const uint32_t bones_a_vertex = std::clamp<uint32_t>(mesh->unknowna8_, 1, 3);
    if (vertices) {
        for (uint32_t i = 0; i < mesh->vertex_count(); ++i) {
            SkinVertex v{};
            std::memcpy(&v, in + i, sizeof(v)); // (the copy may sit unaligned)
            Thug2Vertex& o = vertices[i];
            o = {};
            std::memcpy(o.position, v.position.data(), sizeof(o.position));
            std::memcpy(o.normal, v.normal.data(), sizeof(o.normal));
            o.color = v.color;
            o.uv[0] = v.uv[0];
            o.uv[1] = v.uv[1];
            // (as the renderer skins: the first bone alone when its weight is 1, else the first two or three)
            float weights[3] = {v.weights[0], v.weights[1], v.weights[2]};
            if (bones_a_vertex == 1 || weights[0] == 1.0F) {
                weights[0] = 1.0F;
                weights[1] = weights[2] = 0.0F;
            } else if (bones_a_vertex == 2) {
                weights[2] = 0.0F;
            }
            for (int k = 0; k < 3; ++k) {
                o.bones[k] = v.bones[k];
                o.weights[k] = weights[k];
            }
        }
    }
    if (indices) {
        const std::vector<uint16_t> list = triangle_list(mesh);
        std::copy(list.begin(), list.end(), indices);
    }
    return 0;
}

extern "C" uint32_t thug2_skater_texture(uint32_t index, Thug2Texture* out, void* bits, uint32_t size)
{
    const std::vector<render::TextureImage*>& list = textures();
    if (index >= list.size()) return static_cast<uint32_t>(list.size());
    headless::TextureLevel level{};
    if (!headless::texture_level(list[index]->texture, 0, &level)) return static_cast<uint32_t>(list.size());
    if (out) {
        *out = {level.width, level.height, format_of(level.format), static_cast<uint32_t>(level.bytes)};
    }
    if (bits && size >= level.bytes) std::memcpy(bits, level.bits, level.bytes);
    return static_cast<uint32_t>(list.size());
}

extern "C" uint32_t thug2_skater_pose(float (*palette)[16], uint32_t max, float draw_matrix[16])
{
    objects::GameEntity* entity = skater();
    if (!entity) return 0;
    auto* skeleton = static_cast<objects::BoneRigModule*>(entity->fetch_module(kSkeletonModule));
    gfx::Rig* rig = skeleton ? skeleton->bone_rig() : nullptr;
    if (!rig) return 0;
    const auto matrices = rig->matrices();
    if (palette) {
        for (uint32_t i = 0; i < matrices.size() && i < max; ++i) std::memcpy(palette[i], &matrices[i], sizeof(float) * 16);
    }
    if (draw_matrix) {
        maths::Mat4 draw{};
        if (objects::MeshInstanceModule* module = model_module(entity)) module->get_draw_matrix(&draw);
        std::memcpy(draw_matrix, &draw, sizeof(float) * 16);
    }
    return static_cast<uint32_t>(matrices.size());
}
