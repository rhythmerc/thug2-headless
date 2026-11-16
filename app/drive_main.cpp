// thug2-drive: the engine API (app/thug2_engine.h) driven from the command line, as a host would drive it. Boots the
// game, steps THUG2_FRAMES frames with a scripted pad and logs the skater every 30 frames, then the time a step took.
//     THUG2_GAME_DIR=~/Games/THUG2-extract/Game THUG2_FRAMES=700 THUG2_INPUT="120-330:up,400-412:cross" \
//         thug2-drive -level BO
// THUG2_INPUT: comma-separated "<from>[-<to>]:<control>" items (frames from the first step); controls: up / down /
// left / right (the left stick, full over), rup / rdown / rleft / rright (the right stick), cross, circle, square,
// triangle, l1, r1, l2, r2.
#include "app/thug2_engine.h"
#include "stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <string>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace {

struct Press {
    uint32_t from = 0;
    uint32_t to = 0;
    std::string control;
};

std::vector<Press> parse(const char* text)
{
    std::vector<Press> out;
    const std::string all = text ? text : "";
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find(',', at);
        if (end == std::string::npos) end = all.size();
        const std::string item = all.substr(at, end - at);
        at = end + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        Press p;
        const std::string frames = item.substr(0, colon);
        const size_t dash = frames.find('-');
        p.from = static_cast<uint32_t>(std::strtoul(frames.c_str(), nullptr, 10));
        p.to = dash == std::string::npos ? p.from + 2 : static_cast<uint32_t>(std::strtoul(frames.c_str() + dash + 1, nullptr, 10));
        p.control = item.substr(colon + 1);
        out.push_back(p);
    }
    return out;
}

Thug2Pad pad_at(const std::vector<Press>& script, uint32_t frame)
{
    Thug2Pad pad{0x80, 0x80, 0x80, 0x80, 0};
    for (const Press& p : script) {
        if (frame < p.from || frame > p.to) continue;
        const std::string& c = p.control;
        if (c == "up") pad.left_y = 0;
        else if (c == "down") pad.left_y = 0xFF;
        else if (c == "left") pad.left_x = 0;
        else if (c == "right") pad.left_x = 0xFF;
        else if (c == "rup") pad.right_y = 0;
        else if (c == "rdown") pad.right_y = 0xFF;
        else if (c == "rleft") pad.right_x = 0;
        else if (c == "rright") pad.right_x = 0xFF;
        else if (c == "cross") pad.buttons |= THUG2_CROSS;
        else if (c == "circle") pad.buttons |= THUG2_CIRCLE;
        else if (c == "square") pad.buttons |= THUG2_SQUARE;
        else if (c == "triangle") pad.buttons |= THUG2_TRIANGLE;
        else if (c == "l1") pad.buttons |= THUG2_L1;
        else if (c == "r1") pad.buttons |= THUG2_R1;
        else if (c == "l2") pad.buttons |= THUG2_L2;
        else if (c == "r2") pad.buttons |= THUG2_R2;
    }
    return pad;
}

// THUG2_WORLD=test: a small park far from the level (around x = z = 50000, the floor at y = 0), in game units
// (inches): an 800-inch floor, a 10-foot quarter pipe at +z (its top third vert) with a deck behind it, and a
// wallride wall along -x. THUG2_WINDING=flip turns every triangle round.
std::vector<Thug2Triangle> test_world()
{
    std::vector<Thug2Triangle> out;
    const bool flip = std::getenv("THUG2_WINDING") && std::string(std::getenv("THUG2_WINDING")) == "flip";
    const float cx = 50000.0F, cz = 50000.0F;
    auto tri = [&](const float a[3], const float b[3], const float c[3], uint16_t flags) {
        Thug2Triangle t{};
        const float* v[3] = {a, b, c};
        if (flip) std::swap(v[1], v[2]);
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 3; ++k) t.v[i][k] = v[i][k];
        t.flags = flags;
        out.push_back(t);
    };
    // A quad a b c d (counter-clockwise seen from its front) as two triangles.
    auto quad = [&](std::array<float, 3> a, std::array<float, 3> b, std::array<float, 3> c, std::array<float, 3> d, uint16_t flags) {
        tri(a.data(), b.data(), c.data(), flags);
        tri(a.data(), c.data(), d.data(), flags);
    };
    const float half = 400.0F;
    const float qp_start = cz + 100.0F; // where the quarter pipe's curve starts (along +z)
    const float radius = 120.0F;
    // The floor, up to the quarter pipe.
    quad({cx - half, 0, cz - half}, {cx - half, 0, qp_start}, {cx + half, 0, qp_start}, {cx + half, 0, cz - half}, THUG2_FACE_SKATABLE);
    // The quarter pipe: 12 steps of a quarter circle from flat to vertical, along +z; the steep top third vert.
    const int steps = 12;
    for (int i = 0; i < steps; ++i) {
        const double a0 = (M_PI / 2) * i / steps, a1 = (M_PI / 2) * (i + 1) / steps;
        const float z0 = qp_start + radius * static_cast<float>(std::sin(a0)), y0 = radius * static_cast<float>(1 - std::cos(a0));
        const float z1 = qp_start + radius * static_cast<float>(std::sin(a1)), y1 = radius * static_cast<float>(1 - std::cos(a1));
        const uint16_t flags = THUG2_FACE_SKATABLE | (i >= (std::getenv("THUG2_SPINE") ? steps / 2 : steps * 2 / 3) ? THUG2_FACE_VERT : 0);
        quad({cx - half, y0, z0}, {cx - half, y1, z1}, {cx + half, y1, z1}, {cx + half, y0, z0}, flags);
    }
    const float top_z = qp_start + radius;
    // THUG2_SPINE=1: a second quarter pipe back to back with it (a spine) in place of the deck - its curve coming down
    // the far side, facing +z; both curves vert from 45 degrees up (as the game's spine transfer looks for).
    static const bool spine = std::getenv("THUG2_SPINE") != nullptr;
    if (spine) {
        for (int i = 0; i < steps; ++i) {
            const double a0 = (M_PI / 2) * i / steps, a1 = (M_PI / 2) * (i + 1) / steps;
            const float z0 = top_z + radius * static_cast<float>(1 - std::sin(a0)), y0 = radius * static_cast<float>(1 - std::cos(a0));
            const float z1 = top_z + radius * static_cast<float>(1 - std::sin(a1)), y1 = radius * static_cast<float>(1 - std::cos(a1));
            const uint16_t flags = THUG2_FACE_SKATABLE | (i >= steps / 2 ? THUG2_FACE_VERT : 0);
            // (wound to face +z and up: the far side)
            quad({cx + half, y0, z0}, {cx + half, y1, z1}, {cx - half, y1, z1}, {cx - half, y0, z0}, flags);
        }
        quad({cx - half, 0, top_z + radius}, {cx - half, 0, top_z + radius + 300}, {cx + half, 0, top_z + radius + 300}, {cx + half, 0, top_z + radius}, THUG2_FACE_SKATABLE);
    } else {
        // The deck behind it.
        quad({cx - half, radius, top_z}, {cx - half, radius, top_z + 60}, {cx + half, radius, top_z + 60}, {cx + half, radius, top_z}, THUG2_FACE_SKATABLE);
    }
    // A wallride wall along -x, facing +x.
    quad({cx - half, 0, cz - half}, {cx - half, 120, cz - half}, {cx - half, 120, qp_start}, {cx - half, 0, qp_start}, THUG2_FACE_WALL | THUG2_FACE_SKATABLE);
    return out;
}

// THUG2_SHOT=<file.png> (at THUG2_SHOT_FRAME, default 200): the skater as the host would draw it - every mesh
// skinned with the frame's palette and placed by the draw matrix - rasterised, front and side by side (flat shaded
// by its normals, a z-buffer each; no textures), and its meshes, textures and bones listed.
void shoot(const char* file)
{
    const uint32_t mesh_count = thug2_skater_meshes(nullptr, 0);
    std::vector<Thug2Mesh> meshes(mesh_count);
    thug2_skater_meshes(meshes.data(), mesh_count);
    std::vector<std::array<float, 16>> palette(64);
    float draw[16];
    const uint32_t bones = thug2_skater_pose(reinterpret_cast<float(*)[16]>(palette.data()), 64, draw);
    std::printf("SHOT %u meshes, %u bones\n", mesh_count, bones);
    // Row vectors: p' = p M.
    auto apply = [](const float* m, const float* p, float w) {
        std::array<float, 3> o{};
        for (int c = 0; c < 3; ++c) o[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + w * m[12 + c];
        return o;
    };
    struct Tri { std::array<std::array<float, 3>, 3> p; std::array<float, 3> n; };
    std::vector<Tri> tris;
    std::array<float, 3> lo{1e30F, 1e30F, 1e30F}, hi{-1e30F, -1e30F, -1e30F};
    for (uint32_t m = 0; m < mesh_count; ++m) {
        std::vector<Thug2Vertex> v(meshes[m].vertex_count);
        std::vector<uint16_t> idx(meshes[m].index_count);
        thug2_skater_mesh_data(m, v.data(), idx.data());
        Thug2Texture tex{};
        if (meshes[m].texture >= 0) thug2_skater_texture(static_cast<uint32_t>(meshes[m].texture), &tex, nullptr, 0);
        std::printf("SHOT mesh %u: %u vertices, %u indices, texture %d (%ux%u format %u), two-sided %u, alpha ref %u, blended %u\n",
                    m, meshes[m].vertex_count, meshes[m].index_count, meshes[m].texture, tex.width, tex.height,
                    tex.format, meshes[m].two_sided, meshes[m].alpha_ref, meshes[m].blended);
        std::vector<std::array<float, 3>> posed(v.size());
        for (size_t i = 0; i < v.size(); ++i) {
            std::array<float, 3> sum{};
            for (int k = 0; k < 3; ++k) {
                if (v[i].weights[k] == 0.0F || v[i].bones[k] >= bones) continue;
                const auto q = apply(palette[v[i].bones[k]].data(), v[i].position, 1.0F);
                for (int c = 0; c < 3; ++c) sum[c] += q[c] * v[i].weights[k];
            }
            posed[i] = apply(draw, sum.data(), 1.0F);
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], posed[i][c]);
                hi[c] = std::max(hi[c], posed[i][c]);
            }
        }
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            Tri t{{posed[idx[i]], posed[idx[i + 1]], posed[idx[i + 2]]}, {}};
            const float ax = t.p[1][0] - t.p[0][0], ay = t.p[1][1] - t.p[0][1], az = t.p[1][2] - t.p[0][2];
            const float bx = t.p[2][0] - t.p[0][0], by = t.p[2][1] - t.p[0][1], bz = t.p[2][2] - t.p[0][2];
            t.n = {ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx};
            const float l = std::sqrt(t.n[0] * t.n[0] + t.n[1] * t.n[1] + t.n[2] * t.n[2]);
            if (l > 0) for (float& c : t.n) c /= l;
            tris.push_back(t);
        }
    }
    std::printf("SHOT bounds (%.1f %.1f %.1f) - (%.1f %.1f %.1f), %zu triangles\n", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], tris.size());
    const int size = 400;
    std::vector<uint8_t> image(static_cast<size_t>(size) * 2 * size * 3, 40);
    const float span = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}) * 1.1F;
    for (int view = 0; view < 2; ++view) {
        std::vector<float> depth(static_cast<size_t>(size) * size, -1e30F);
        const int across = view == 0 ? 0 : 2; // front: x across, z depth; side: z across, x depth
        const int deep = view == 0 ? 2 : 0;
        for (const Tri& t : tris) {
            std::array<float, 3> sx{}, sy{}, sz{};
            for (int k = 0; k < 3; ++k) {
                sx[k] = (t.p[k][across] - (lo[across] + hi[across]) / 2) / span * size + size / 2.0F;
                sy[k] = size - ((t.p[k][1] - lo[1]) / span * size + size * 0.05F);
                sz[k] = t.p[k][deep];
            }
            const float shade = 0.25F + 0.75F * std::fabs(t.n[deep] * 0.6F + t.n[1] * 0.4F + t.n[across] * 0.2F);
            const int x0 = std::max(0, static_cast<int>(std::min({sx[0], sx[1], sx[2]})));
            const int x1 = std::min(size - 1, static_cast<int>(std::max({sx[0], sx[1], sx[2]})));
            const int y0 = std::max(0, static_cast<int>(std::min({sy[0], sy[1], sy[2]})));
            const int y1 = std::min(size - 1, static_cast<int>(std::max({sy[0], sy[1], sy[2]})));
            const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
            if (std::fabs(area) < 1e-6F) continue;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float px = x + 0.5F, py = y + 0.5F;
                    const float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
                    const float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
                    const float w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    const float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                    float& d = depth[static_cast<size_t>(y) * size + x];
                    if (z <= d) continue;
                    d = z;
                    uint8_t* px_out = &image[(static_cast<size_t>(y) * size * 2 + view * size + x) * 3];
                    px_out[0] = static_cast<uint8_t>(230 * shade);
                    px_out[1] = static_cast<uint8_t>(150 * shade);
                    px_out[2] = static_cast<uint8_t>(90 * shade);
                }
            }
        }
    }
    stbi_write_png(file, size * 2, size, 3, image.data(), size * 2 * 3);
    std::printf("SHOT written to %s\n", file);
}

const char* state_name(int state)
{
    static const char* names[] = {"ground", "air", "wall", "lip", "rail", "wallplant"};
    return state >= 0 && state <= 5 ? names[state] : "?";
}

// `THUG2_SOUNDS=<folder>`: each voice the game starts is logged (DRIVE sound ...), and each sound it plays is written
// to the folder once as a .wav, to listen to.
void log_sounds(uint32_t frame)
{
    static const char* folder = std::getenv("THUG2_SOUNDS");
    if (!folder) return;
    static std::map<uint32_t, uint32_t> starts; // voice -> starts seen
    static std::set<uint32_t> written;
    std::vector<Thug2Voice> voices(thug2_sound_voices(nullptr, 0));
    voices.resize(thug2_sound_voices(voices.data(), static_cast<uint32_t>(voices.size())));
    for (const Thug2Voice& v : voices) {
        static const uint32_t every = std::getenv("THUG2_EVERY") ? static_cast<uint32_t>(std::strtoul(std::getenv("THUG2_EVERY"), nullptr, 10)) : 30;
        if (v.looping && frame % every == 0) {
            std::printf("DRIVE frame %u: sound %u looping on voice %u volume %d pan %d at %u Hz\n", frame, v.sound, v.voice, v.volume, v.pan, v.frequency);
        }
        if (starts[v.voice] == v.starts) continue;
        starts[v.voice] = v.starts;
        std::printf("DRIVE frame %u: sound %u on voice %u%s volume %d pan %d at %u Hz\n", frame, v.sound, v.voice,
                    v.looping ? " looping" : "", v.volume, v.pan, v.frequency);
        if (!written.insert(v.sound).second) continue;
        Thug2Sound info{};
        if (thug2_sound(v.sound, &info, nullptr, 0) != 0) continue;
        std::vector<int16_t> samples(info.samples);
        thug2_sound(v.sound, nullptr, samples.data(), info.samples);
        const std::string name = std::string(folder) + "/sound-" + std::to_string(v.sound) + ".wav";
        FILE* f = std::fopen(name.c_str(), "wb");
        if (!f) continue;
        const uint32_t data = info.samples * 2, channels = info.channels ? info.channels : 1;
        const uint32_t header[] = {0x46464952, 36 + data, 0x45564157, 0x20746D66, 16, 1 | channels << 16, info.rate,
                                   info.rate * channels * 2, (channels * 2) | 16 << 16, 0x61746164, data};
        std::fwrite(header, sizeof(header), 1, f);
        std::fwrite(samples.data(), 2, samples.size(), f);
        std::fclose(f);
    }
}

// THUG2_HUD_SHOT=<file.png> (at THUG2_HUD_FRAME, default 200): the frame's 2D as the host gets it (thug2_hud),
// rasterised over grey at 640x480: each draw's texture times its colour doubled, blended as the game blends it.
void shoot_hud(const char* file)
{
    uint32_t vertex_count = 0;
    const uint32_t count = thug2_hud(nullptr, 0, nullptr, 0, &vertex_count);
    std::vector<Thug2HudDraw> draws(count);
    std::vector<Thug2HudVertex> vertices(vertex_count);
    thug2_hud(draws.data(), count, vertices.data(), vertex_count, nullptr);
    constexpr int W = 640, H = 480;
    std::vector<float> image(static_cast<size_t>(W) * H * 3, 0.3F);
    std::map<int32_t, std::tuple<uint32_t, uint32_t, std::vector<uint8_t>>> textures;
    for (const Thug2HudDraw& d : draws) {
        if (d.texture >= 0 && !textures.count(d.texture)) {
            uint32_t w = 0, h = 0;
            thug2_hud_texture(d.texture, &w, &h, nullptr, 0);
            std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
            if (thug2_hud_texture(d.texture, &w, &h, rgba.data(), static_cast<uint32_t>(rgba.size())) != 0) rgba.clear();
            textures[d.texture] = {w, h, std::move(rgba)};
        }
        const auto* texture = d.texture >= 0 && !std::get<2>(textures[d.texture]).empty() ? &textures[d.texture] : nullptr;
        for (uint32_t t = 0; t + 2 < d.vertex_count; t += 3) {
            const Thug2HudVertex* v = &vertices[d.first_vertex + t];
            const float x0 = std::min({v[0].x, v[1].x, v[2].x}), x1 = std::max({v[0].x, v[1].x, v[2].x});
            const float y0 = std::min({v[0].y, v[1].y, v[2].y}), y1 = std::max({v[0].y, v[1].y, v[2].y});
            const float area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y);
            if (std::fabs(area) < 1e-6F) continue;
            for (int y = std::max(0, static_cast<int>(y0)); y < std::min(H, static_cast<int>(std::ceil(y1))); ++y) {
                for (int x = std::max(0, static_cast<int>(x0)); x < std::min(W, static_cast<int>(std::ceil(x1))); ++x) {
                    const float px = x + 0.5F, py = y + 0.5F;
                    const float b1 = ((px - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (py - v[0].y)) / area;
                    const float b2 = ((v[1].x - v[0].x) * (py - v[0].y) - (px - v[0].x) * (v[1].y - v[0].y)) / area;
                    const float b0 = 1.0F - b1 - b2;
                    if (b0 < 0 || b1 < 0 || b2 < 0) continue;
                    float c[4];
                    for (int k = 0; k < 4; ++k) {
                        const int shift = k == 3 ? 24 : 16 - 8 * k; // r, g, b, a from ARGB
                        c[k] = (b0 * ((v[0].color >> shift) & 255) + b1 * ((v[1].color >> shift) & 255) + b2 * ((v[2].color >> shift) & 255)) / 255.0F;
                    }
                    if (texture && d.colour_op != 3) {
                        const auto& [tw, th, rgba] = *texture;
                        const float u = b0 * v[0].u + b1 * v[1].u + b2 * v[2].u, vv = b0 * v[0].v + b1 * v[1].v + b2 * v[2].v;
                        const uint32_t tx = std::min(tw - 1, static_cast<uint32_t>(std::max(0.0F, u * tw)));
                        const uint32_t ty = std::min(th - 1, static_cast<uint32_t>(std::max(0.0F, vv * th)));
                        const uint8_t* texel = &rgba[(static_cast<size_t>(ty) * tw + tx) * 4];
                        const float doubled = d.colour_op == 5 ? 2.0F : 1.0F;
                        for (int k = 0; k < 4; ++k) c[k] = std::min(1.0F, c[k] * texel[k] / 255.0F * doubled);
                    }
                    float* o = &image[(static_cast<size_t>(y) * W + x) * 3];
                    const bool added = d.blend && d.dest_blend == 2;
                    const float a = d.blend ? c[3] : 1.0F;
                    for (int k = 0; k < 3; ++k) o[k] = added ? std::min(1.0F, o[k] + c[k] * a) : o[k] * (1 - a) + c[k] * a;
                }
            }
        }
    }
    std::vector<uint8_t> bytes(image.size());
    for (size_t i = 0; i < image.size(); ++i) bytes[i] = static_cast<uint8_t>(std::clamp(image[i], 0.0F, 1.0F) * 255.0F + 0.5F);
    stbi_write_png(file, W, H, 3, bytes.data(), W * 3);
    std::printf("DRIVE the 2D (%u draws, %u vertices, %zu textures) written to %s\n", count, vertex_count, textures.size(), file);
    for (const Thug2HudDraw& d : draws) {
        const Thug2HudVertex& v = vertices[d.first_vertex];
        std::printf("DRIVE   draw: %u vertices texture %d blend %u %u/%u op %u at (%.0f, %.0f) colour %08x\n", d.vertex_count, d.texture, d.blend,
                    d.source_blend, d.dest_blend, d.colour_op, v.x, v.y, v.color);
    }
}

} // namespace

int main(int argc, char** argv)
{
    using clock = std::chrono::steady_clock;
    const auto boot_start = clock::now();
    if (thug2_boot(std::getenv("THUG2_GAME_DIR"), argc, argv) != 0) {
        std::fprintf(stderr, "thug2-drive: the game ended before its first frame\n");
        return 1;
    }
    const double boot_ms = std::chrono::duration<double, std::milli>(clock::now() - boot_start).count();
    std::printf("DRIVE booted in %.0f ms\n", boot_ms);
    const char* frames_text = std::getenv("THUG2_FRAMES");
    const uint32_t frames = frames_text ? static_cast<uint32_t>(std::strtoul(frames_text, nullptr, 10)) : 600;
    const std::vector<Press> script = parse(std::getenv("THUG2_INPUT"));
    double total_ms = 0;
    double worst_ms = 0;
    uint32_t stepped = 0;
    const bool test = std::getenv("THUG2_WORLD") && std::string(std::getenv("THUG2_WORLD")) == "test";
    const std::vector<Thug2Triangle> world = test ? test_world() : std::vector<Thug2Triangle>{};
    if (std::getenv("THUG2_HUD_SHOT")) thug2_hud_enable(1);
    for (uint32_t frame = 1; frame <= frames; ++frame) {
        // (the world once the level has put the skater at its own start, else that start overrides ours)
        if (test && frame == 120) {
            const float spawn[3] = {50000.0F, 20.0F, 49700.0F};
            thug2_set_world(world.data(), static_cast<uint32_t>(world.size()), spawn, 0.0F);
            // A ledge-height rail down the middle of the run, 30 units up, before the quarter pipe.
            const float rail[2][3] = {{50000.0F, 30.0F, 49900.0F}, {50000.0F, 30.0F, 50080.0F}};
            const uint32_t counts[1] = {2};
            thug2_set_rails(rail, counts, 1);
            std::printf("DRIVE frame %u: the test world (%zu triangles) given\n", frame, world.size());
        }
        // THUG2_REBUILD=<frame>: the same world given again then, with no spawn (the skater kept where it is).
        static const uint32_t rebuild = std::getenv("THUG2_REBUILD") ? static_cast<uint32_t>(std::strtoul(std::getenv("THUG2_REBUILD"), nullptr, 10)) : 0;
        if (test && rebuild && frame == rebuild) {
            thug2_set_world(world.data(), static_cast<uint32_t>(world.size()), nullptr, 0.0F);
            std::printf("DRIVE frame %u: the test world given again (no spawn)\n", frame);
        }
        static const uint32_t shot_frame = std::getenv("THUG2_SHOT_FRAME") ? static_cast<uint32_t>(std::strtoul(std::getenv("THUG2_SHOT_FRAME"), nullptr, 10)) : 200;
        if (std::getenv("THUG2_SHOT") && frame == shot_frame + 1) shoot(std::getenv("THUG2_SHOT"));
        const Thug2Pad pad = pad_at(script, frame);
        Thug2Skater skater{};
        const auto start = clock::now();
        if (thug2_step(&pad, &skater) != 0) {
            std::printf("DRIVE the game ended at frame %u\n", frame);
            break;
        }
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - start).count();
        ++stepped;
        total_ms += ms;
        if (ms > worst_ms) worst_ms = ms;
        if (ms > 20.0) std::printf("DRIVE frame %u took %.0f ms\n", frame, ms);
        log_sounds(frame);
        static const uint32_t hud_frame = std::getenv("THUG2_HUD_FRAME") ? static_cast<uint32_t>(std::strtoul(std::getenv("THUG2_HUD_FRAME"), nullptr, 10)) : 200;
        if (std::getenv("THUG2_HUD_SHOT") && frame == hud_frame) shoot_hud(std::getenv("THUG2_HUD_SHOT"));
        static const uint32_t every = std::getenv("THUG2_EVERY") ? static_cast<uint32_t>(std::strtoul(std::getenv("THUG2_EVERY"), nullptr, 10)) : 30;
        if (frame % every == 0) {
            if (skater.present) {
                std::printf("DRIVE frame %u: at (%.1f, %.1f, %.1f) vel (%.1f, %.1f, %.1f) fwd (%.2f, %.2f, %.2f) %s balance %d %.2f  [%.2f ms]\n", frame,
                            skater.position[0], skater.position[1], skater.position[2], skater.velocity[0],
                            skater.velocity[1], skater.velocity[2], skater.forward[0], skater.forward[1],
                            skater.forward[2], state_name(skater.state), skater.balance_kind, skater.balance, ms);
            } else {
                std::printf("DRIVE frame %u: no skater yet  [%.2f ms]\n", frame, ms);
            }
            std::fflush(stdout);
        }
    }
    if (stepped) std::printf("DRIVE %u steps: %.2f ms average, %.2f ms worst\n", stepped, total_ms / stepped, worst_ms);
    thug2_quit();
    return 0;
}
