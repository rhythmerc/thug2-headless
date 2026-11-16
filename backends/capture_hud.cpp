// The game's 2D (its HUD: the trick text, the score, the special meter, goal messages...) for a host that draws it.
//
// Every frame's 2D is one list of texts and sprites, drawn in priority order by render::draw_2d (render_pc/draw2d.h);
// each item draws pre-transformed screen quads (x, y in the back buffer's pixels, a colour, one texture). With the HUD
// asked for, at the end of each frame (in the game's thread, as the frame's last work) the list is drawn once more with
// the in-memory GPU taking those draws down instead of dropping them (capturing()); the host reads them between steps
// (app/thug2_engine.h, thug2_hud*). Textures are named by an id the host can keep: one texture keeps its id while it
// lives, and a new texture never gets an old one's.
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "app/thug2_engine.h"
#include "backends/backends.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/gpu.h"
#include "render_pc/draw2d.h"

namespace {

constexpr uint32_t kScreenVertexFormat = 0x144; // xyzrhw, diffuse, one texture coordinate
constexpr uint32_t kIndex32 = 102;

struct Capture {
    bool wanted = false;
    bool capturing = false;
    std::vector<Thug2HudDraw> draws;
    std::vector<Thug2HudVertex> vertices;
    std::map<render::GpuTexture*, int32_t> ids;
    std::map<int32_t, render::GpuTexture*> textures;
    int32_t next_id = 1;
};

Capture& capture()
{
    static Capture c;
    return c;
}

int32_t id_of(render::GpuTexture* texture)
{
    if (!texture) return -1;
    Capture& c = capture();
    auto [at, made] = c.ids.emplace(texture, c.next_id);
    if (made) {
        c.textures[c.next_id] = texture;
        ++c.next_id;
    }
    return at->second;
}

// A screen vertex (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1).
Thug2HudVertex vertex_at(const uint8_t* bytes)
{
    Thug2HudVertex v{};
    std::memcpy(&v.x, bytes, 4);
    std::memcpy(&v.y, bytes + 4, 4);
    std::memcpy(&v.color, bytes + 16, 4);
    std::memcpy(&v.u, bytes + 20, 4);
    std::memcpy(&v.v, bytes + 24, 4);
    return v;
}

// --- texels to RGBA8 ---

uint32_t expand(uint32_t value, uint32_t bits) { return bits == 0 ? 255 : (value * 255 + ((1U << bits) - 1) / 2) / ((1U << bits) - 1); }

void rgb565(uint16_t c, uint8_t* out)
{
    out[0] = static_cast<uint8_t>(expand((c >> 11) & 31, 5));
    out[1] = static_cast<uint8_t>(expand((c >> 5) & 63, 6));
    out[2] = static_cast<uint8_t>(expand(c & 31, 5));
}

// One DXT block's 16 colours (alpha 255; DXT1's punch-through 0) into `out` (16 x RGBA).
void dxt_colours(const uint8_t* block, bool dxt1, uint8_t out[16][4])
{
    const uint16_t c0 = static_cast<uint16_t>(block[0] | block[1] << 8);
    const uint16_t c1 = static_cast<uint16_t>(block[2] | block[3] << 8);
    uint8_t p[4][4]{};
    rgb565(c0, p[0]);
    rgb565(c1, p[1]);
    p[0][3] = p[1][3] = p[2][3] = p[3][3] = 255;
    if (c0 > c1 || !dxt1) {
        for (int k = 0; k < 3; ++k) {
            p[2][k] = static_cast<uint8_t>((2 * p[0][k] + p[1][k] + 1) / 3);
            p[3][k] = static_cast<uint8_t>((p[0][k] + 2 * p[1][k] + 1) / 3);
        }
    } else {
        for (int k = 0; k < 3; ++k) p[2][k] = static_cast<uint8_t>((p[0][k] + p[1][k]) / 2);
        p[3][3] = 0;
    }
    const uint32_t bits = static_cast<uint32_t>(block[4] | block[5] << 8 | block[6] << 16 | block[7] << 24);
    for (int i = 0; i < 16; ++i) std::memcpy(out[i], p[(bits >> (2 * i)) & 3], 4);
}

bool to_rgba(const headless::TextureLevel& level, uint8_t* out)
{
    const uint32_t w = level.width, h = level.height, f = level.format;
    auto fourcc = [](char a, char b, char c, char d) {
        return static_cast<uint32_t>(a) | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24;
    };
    const bool dxt1 = f == fourcc('D', 'X', 'T', '1');
    const bool dxt3 = f == fourcc('D', 'X', 'T', '2') || f == fourcc('D', 'X', 'T', '3');
    const bool dxt5 = f == fourcc('D', 'X', 'T', '4') || f == fourcc('D', 'X', 'T', '5');
    if (dxt1 || dxt3 || dxt5) {
        const uint32_t block_bytes = dxt1 ? 8 : 16;
        for (uint32_t by = 0; by < (h + 3) / 4; ++by) {
            for (uint32_t bx = 0; bx < (w + 3) / 4; ++bx) {
                const size_t at = static_cast<size_t>(by) * level.pitch + static_cast<size_t>(bx) * block_bytes;
                if (at + block_bytes > level.bytes) return false;
                const uint8_t* block = level.bits + at;
                uint8_t colours[16][4];
                dxt_colours(block + (dxt1 ? 0 : 8), dxt1, colours);
                uint8_t alpha[16];
                for (int i = 0; i < 16; ++i) alpha[i] = colours[i][3];
                if (dxt3) {
                    for (int i = 0; i < 16; ++i) alpha[i] = static_cast<uint8_t>(expand((block[i / 2] >> (4 * (i % 2))) & 15, 4));
                } else if (dxt5) {
                    uint8_t a[8];
                    a[0] = block[0];
                    a[1] = block[1];
                    if (a[0] > a[1]) {
                        for (int k = 1; k < 7; ++k) a[k + 1] = static_cast<uint8_t>(((7 - k) * a[0] + k * a[1] + 3) / 7);
                    } else {
                        for (int k = 1; k < 5; ++k) a[k + 1] = static_cast<uint8_t>(((5 - k) * a[0] + k * a[1] + 2) / 5);
                        a[6] = 0;
                        a[7] = 255;
                    }
                    uint64_t bits = 0;
                    for (int k = 0; k < 6; ++k) bits |= static_cast<uint64_t>(block[2 + k]) << (8 * k);
                    for (int i = 0; i < 16; ++i) alpha[i] = a[(bits >> (3 * i)) & 7];
                }
                for (int i = 0; i < 16; ++i) {
                    const uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                    if (x >= w || y >= h) continue;
                    uint8_t* o = out + (static_cast<size_t>(y) * w + x) * 4;
                    std::memcpy(o, colours[i], 3);
                    o[3] = alpha[i];
                }
            }
        }
        return true;
    }
    uint32_t bytes_per = 4;
    switch (f) {
    case 21: case 22: bytes_per = 4; break;           // A8R8G8B8, X8R8G8B8
    case 23: case 24: case 25: case 26: case 51: bytes_per = 2; break; // R5G6B5, X1R5G5B5, A1R5G5B5, A4R4G4B4, A8L8
    case 28: case 50: bytes_per = 1; break;            // A8, L8
    default: return false;
    }
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const size_t at = static_cast<size_t>(y) * level.pitch + static_cast<size_t>(x) * bytes_per;
            if (at + bytes_per > level.bytes) return false;
            const uint8_t* s = level.bits + at;
            uint8_t* o = out + (static_cast<size_t>(y) * w + x) * 4;
            const uint16_t p16 = static_cast<uint16_t>(s[0] | (bytes_per > 1 ? s[1] << 8 : 0));
            switch (f) {
            case 21: case 22: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = f == 22 ? 255 : s[3]; break;
            case 23: rgb565(p16, o); o[3] = 255; break;
            case 24: case 25:
                o[0] = static_cast<uint8_t>(expand((p16 >> 10) & 31, 5));
                o[1] = static_cast<uint8_t>(expand((p16 >> 5) & 31, 5));
                o[2] = static_cast<uint8_t>(expand(p16 & 31, 5));
                o[3] = f == 24 || (p16 & 0x8000) ? 255 : 0;
                break;
            case 26:
                o[0] = static_cast<uint8_t>(expand((p16 >> 8) & 15, 4));
                o[1] = static_cast<uint8_t>(expand((p16 >> 4) & 15, 4));
                o[2] = static_cast<uint8_t>(expand(p16 & 15, 4));
                o[3] = static_cast<uint8_t>(expand(p16 >> 12, 4));
                break;
            case 51: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;
            case 28: o[0] = o[1] = o[2] = 255; o[3] = s[0]; break;
            case 50: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;
            default: break;
            }
        }
    }
    return true;
}

} // namespace

namespace headless {

bool hud_capturing() { return capture().capturing; }

void hud_texture_released(render::GpuTexture* texture)
{
    Capture& c = capture();
    const auto at = c.ids.find(texture);
    if (at == c.ids.end()) return;
    c.textures.erase(at->second);
    c.ids.erase(at);
}

void hud_record(const render::GpuStateBlock& state, uint32_t primitive, const void* indices, uint32_t index_format,
                uint32_t index_count, const void* vertices, uint32_t vertex_count, uint32_t stride)
{
    Capture& c = capture();
    if (state.vertex_format != kScreenVertexFormat || !vertices || stride < 28) return;
    // (as triangles: a list, a strip or a fan)
    std::vector<uint32_t> order;
    const uint32_t n = indices ? index_count : vertex_count;
    auto index = [&](uint32_t i) -> uint32_t {
        if (!indices) return i;
        return index_format == kIndex32 ? static_cast<const uint32_t*>(indices)[i] : static_cast<const uint16_t*>(indices)[i];
    };
    if (primitive == 4) {
        for (uint32_t i = 0; i + 2 < n; i += 3) order.insert(order.end(), {index(i), index(i + 1), index(i + 2)});
    } else if (primitive == 5) {
        for (uint32_t i = 0; i + 2 < n; ++i) order.insert(order.end(), {index(i), index(i + 1), index(i + 2)});
    } else if (primitive == 6) {
        for (uint32_t i = 1; i + 1 < n; ++i) order.insert(order.end(), {index(0), index(i), index(i + 1)});
    } else {
        return;
    }
    if (order.empty()) return;
    Thug2HudDraw d{};
    d.first_vertex = static_cast<uint32_t>(c.vertices.size());
    d.vertex_count = static_cast<uint32_t>(order.size());
    d.texture = id_of(state.textures[0]);
    d.blend = state.render_state(render::GpuRenderState::kAlphaBlendEnable) ? 1 : 0;
    d.source_blend = static_cast<uint8_t>(state.render_state(render::GpuRenderState::kSrcBlend));
    d.dest_blend = static_cast<uint8_t>(state.render_state(render::GpuRenderState::kDestBlend));
    d.colour_op = static_cast<uint8_t>(state.stage_state(0, render::GpuStageState::kColourOp));
    d.alpha_op = static_cast<uint8_t>(state.stage_state(0, render::GpuStageState::kAlphaOp));
    const auto* bytes = static_cast<const uint8_t*>(vertices);
    for (uint32_t i : order) c.vertices.push_back(vertex_at(bytes + static_cast<size_t>(i) * stride));
    c.draws.push_back(d);
}

// The frame's 2D taken down (in the game's thread, between its frames), when the host asked for it.
void hud_frame_done()
{
    Capture& c = capture();
    c.draws.clear();
    c.vertices.clear();
    if (!c.wanted) return;
    auto& parts = render::engine_parts();
    const bool suppressed = parts.suppress_draw();
    parts.set_suppress_draw(false);
    c.capturing = true;
    render::draw_2d();
    c.capturing = false;
    parts.set_suppress_draw(suppressed);
}

} // namespace headless

extern "C" void thug2_hud_enable(int on) { capture().wanted = on != 0; }

extern "C" uint32_t thug2_hud(Thug2HudDraw* draws, uint32_t max_draws, Thug2HudVertex* vertices, uint32_t max_vertices,
                              uint32_t* vertex_count)
{
    const Capture& c = capture();
    if (vertex_count) *vertex_count = static_cast<uint32_t>(c.vertices.size());
    if (draws && max_draws >= c.draws.size() && vertices && max_vertices >= c.vertices.size()) {
        std::memcpy(draws, c.draws.data(), c.draws.size() * sizeof(Thug2HudDraw));
        std::memcpy(vertices, c.vertices.data(), c.vertices.size() * sizeof(Thug2HudVertex));
    }
    return static_cast<uint32_t>(c.draws.size());
}

extern "C" int thug2_hud_texture(int32_t texture, uint32_t* width, uint32_t* height, uint8_t* rgba, uint32_t size)
{
    const Capture& c = capture();
    const auto at = c.textures.find(texture);
    if (at == c.textures.end()) return 1;
    headless::TextureLevel level{};
    if (!headless::texture_level(at->second, 0, &level)) return 1;
    if (width) *width = level.width;
    if (height) *height = level.height;
    if (!rgba) return 0;
    if (size < static_cast<size_t>(level.width) * level.height * 4) return 1;
    return to_rgba(level, rgba) ? 0 : 2;
}
