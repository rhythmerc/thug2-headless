// The GPU under render_pc when nothing is drawn: resources are real memory (a texture's levels, a surface, a vertex or
// index buffer), so the renderer can make, lock and fill them exactly as it does on Direct3D 9 - level scenes and the
// skater's model load as they do there - and every draw, state and present is accepted and dropped.
#include "backends/backends.h"
#include "backends/null_stubs.h"

#include <cstring>
#include <memory>
#include <vector>


namespace headless {
namespace {

constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}

// Bytes per 4x4 block (block-compressed formats) or per pixel (the rest), by Direct3D 9's format number.
struct Layout {
    bool blocks = false;
    uint32_t bytes = 4;
};
Layout layout_of(uint32_t format)
{
    switch (format) {
    case fourcc('D', 'X', 'T', '1'):
        return {true, 8};
    case fourcc('D', 'X', 'T', '2'):
    case fourcc('D', 'X', 'T', '3'):
    case fourcc('D', 'X', 'T', '4'):
    case fourcc('D', 'X', 'T', '5'):
        return {true, 16};
    case 20: // R8G8B8
        return {false, 3};
    case 23: // R5G6B5
    case 24: // X1R5G5B5
    case 25: // A1R5G5B5
    case 26: // A4R4G4B4
    case 51: // A8L8
    case 80: // D16
    case 111: // R16F
        return {false, 2};
    case 28: // A8
    case 41: // P8
    case 50: // L8
        return {false, 1};
    case 113: // A16B16G16R16F
        return {false, 8};
    case 116: // A32B32G32R32F
        return {false, 16};
    default: // A8R8G8B8, X8R8G8B8, D24S8, D32, R32F, G16R16F...
        return {false, 4};
    }
}

struct Surface {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    uint32_t pitch = 0;
    std::vector<uint8_t> bits;
    void make(uint32_t w, uint32_t h, uint32_t f)
    {
        width = std::max(w, 1U);
        height = std::max(h, 1U);
        format = f;
        const Layout l = layout_of(f);
        const uint32_t across = l.blocks ? (width + 3) / 4 : width;
        const uint32_t rows = l.blocks ? (height + 3) / 4 : height;
        pitch = across * l.bytes;
        bits.assign(static_cast<size_t>(pitch) * rows, 0);
    }
};

struct Texture {
    std::vector<std::unique_ptr<Surface>> levels;
};

struct Buffer {
    std::vector<uint8_t> bytes;
    uint32_t refs = 1;
};

struct Shader {
    int unused = 0;
};

template <class To, class From>
To* as(From* from)
{
    return reinterpret_cast<To*>(from);
}

class HeadlessGpu final : public render::NullGpu {
public:
    HeadlessGpu() { back_.make(640, 480, 22); }

    long create_texture(const render::GpuTextureDesc& desc, render::GpuTexture** texture) override
    {
        auto* t = new Texture();
        uint32_t w = std::max(desc.width, 1U);
        uint32_t h = std::max(desc.height, 1U);
        uint32_t count = desc.levels;
        if (count == 0) {
            count = 1;
            for (uint32_t s = std::max(w, h); s > 1; s >>= 1) ++count;
        }
        for (uint32_t i = 0; i < count; ++i) {
            auto level = std::make_unique<Surface>();
            level->make(w, h, desc.format);
            t->levels.push_back(std::move(level));
            w = std::max(w / 2, 1U);
            h = std::max(h / 2, 1U);
        }
        *texture = as<render::GpuTexture>(t);
        return 0;
    }
    long lock_texture(render::GpuTexture* texture, uint32_t level, render::GpuLockedRect* locked, const render::GpuRect* area, uint32_t) override
    {
        Surface* s = level_of(texture, level);
        if (!s || !locked) return -1;
        return lock(*s, locked, area);
    }
    long unlock_texture(render::GpuTexture*, uint32_t) override { return 0; }
    long texture_surface(render::GpuTexture* texture, uint32_t level, render::GpuSurface** surface) override
    {
        *surface = as<render::GpuSurface>(level_of(texture, level));
        return *surface ? 0 : -1;
    }
    long texture_level_size(render::GpuTexture* texture, uint32_t level, render::GpuSize* size) override
    {
        Surface* s = level_of(texture, level);
        if (!s) return -1;
        *size = {s->width, s->height};
        return 0;
    }
    long texture_level_desc(render::GpuTexture* texture, uint32_t level, render::GpuLevelDesc* desc) override
    {
        Surface* s = level_of(texture, level);
        if (!s) return -1;
        *desc = {s->width, s->height, s->format};
        return 0;
    }
    uint32_t texture_level_count(render::GpuTexture* texture) override { return texture ? static_cast<uint32_t>(as<Texture>(texture)->levels.size()) : 0; }
    uint32_t release_texture(render::GpuTexture* texture) override
    {
        delete as<Texture>(texture);
        return 0;
    }

    long create_vertex_buffer(uint32_t bytes, uint32_t, uint32_t, uint32_t, render::GpuVertexBuffer** buffer) override
    {
        *buffer = as<render::GpuVertexBuffer>(make_buffer(bytes));
        return 0;
    }
    long lock_vertex_buffer(render::GpuVertexBuffer* buffer, uint32_t offset, uint32_t, void** data, uint32_t) override
    {
        *data = as<Buffer>(buffer)->bytes.data() + offset;
        return 0;
    }
    long unlock_vertex_buffer(render::GpuVertexBuffer*) override { return 0; }
    uint32_t release_vertex_buffer(render::GpuVertexBuffer* buffer) override { return release(as<Buffer>(buffer)); }
    uint32_t retain_vertex_buffer(render::GpuVertexBuffer* buffer) override { return ++as<Buffer>(buffer)->refs; }
    long create_index_buffer(uint32_t bytes, uint32_t, uint32_t, uint32_t, render::GpuIndexBuffer** buffer) override
    {
        *buffer = as<render::GpuIndexBuffer>(make_buffer(bytes));
        return 0;
    }
    long lock_index_buffer(render::GpuIndexBuffer* buffer, uint32_t offset, uint32_t, void** data, uint32_t) override
    {
        *data = as<Buffer>(buffer)->bytes.data() + offset;
        return 0;
    }
    long unlock_index_buffer(render::GpuIndexBuffer*) override { return 0; }
    uint32_t release_index_buffer(render::GpuIndexBuffer* buffer) override { return release(as<Buffer>(buffer)); }

    long create_render_target(const render::GpuSurfaceDesc& desc, render::GpuSurface** surface) override { return make_surface(desc.width, desc.height, desc.format, surface); }
    long create_depth_surface(const render::GpuSurfaceDesc& desc, render::GpuSurface** surface) override { return make_surface(desc.width, desc.height, desc.format, surface); }
    long create_offscreen_surface(uint32_t width, uint32_t height, uint32_t format, uint32_t, render::GpuSurface** surface) override
    {
        return make_surface(width, height, format, surface);
    }
    long render_target(uint32_t, render::GpuSurface** surface) override
    {
        *surface = as<render::GpuSurface>(&back_);
        return 0;
    }
    long depth_surface(render::GpuSurface** surface) override
    {
        *surface = as<render::GpuSurface>(&back_);
        return 0;
    }
    long back_buffer(render::GpuSurface** surface) override
    {
        *surface = as<render::GpuSurface>(&back_);
        return 0;
    }
    uint32_t release_surface(render::GpuSurface* surface) override
    {
        // (a texture's level or the back buffer belongs to them; only surfaces made alone are freed)
        auto* s = as<Surface>(surface);
        for (auto it = own_surfaces_.begin(); it != own_surfaces_.end(); ++it) {
            if (it->get() == s) {
                own_surfaces_.erase(it);
                break;
            }
        }
        return 0;
    }
    long surface_desc(render::GpuSurface* surface, render::GpuSurfaceDesc* desc) override
    {
        auto* s = as<Surface>(surface);
        if (!s) return -1;
        *desc = {};
        desc->width = s->width;
        desc->height = s->height;
        desc->format = s->format;
        return 0;
    }
    long lock_surface(render::GpuSurface* surface, render::GpuLockedRect* locked, const render::GpuRect* area, uint32_t) override
    {
        return surface ? lock(*as<Surface>(surface), locked, area) : -1;
    }
    long unlock_surface(render::GpuSurface*) override { return 0; }
    long display_size(render::GpuSize* size) override
    {
        *size = {back_.width, back_.height};
        return 0;
    }
    long create_pixel_shader(const char*, uint32_t, render::GpuPixelShader** shader) override
    {
        *shader = as<render::GpuPixelShader>(new Shader());
        return 0;
    }
    uint32_t release_pixel_shader(render::GpuPixelShader* shader) override
    {
        delete as<Shader>(shader);
        return 0;
    }
    long create_occlusion_query(render::GpuQuery** query) override
    {
        *query = as<render::GpuQuery>(new Shader());
        return 0;
    }
    long query_result(render::GpuQuery*, void* data, uint32_t bytes, uint32_t) override
    {
        // (everything is visible: a full count)
        if (data && bytes >= sizeof(uint32_t)) *static_cast<uint32_t*>(data) = 1000;
        return 0;
    }
    uint32_t release_query(render::GpuQuery* query) override
    {
        delete as<Shader>(query);
        return 0;
    }

private:
    static Surface* level_of(render::GpuTexture* texture, uint32_t level)
    {
        auto* t = as<Texture>(texture);
        return (t && level < t->levels.size()) ? t->levels[level].get() : nullptr;
    }
    static long lock(Surface& s, render::GpuLockedRect* locked, const render::GpuRect* area)
    {
        const Layout l = layout_of(s.format);
        size_t at = 0;
        if (area) {
            const uint32_t x = static_cast<uint32_t>(area->left);
            const uint32_t y = static_cast<uint32_t>(area->top);
            at = l.blocks ? static_cast<size_t>(y / 4) * s.pitch + static_cast<size_t>(x / 4) * l.bytes
                          : static_cast<size_t>(y) * s.pitch + static_cast<size_t>(x) * l.bytes;
        }
        locked->pitch = static_cast<int32_t>(s.pitch);
        locked->bits = s.bits.data() + std::min(at, s.bits.size());
        return 0;
    }
    static Buffer* make_buffer(uint32_t bytes)
    {
        auto* b = new Buffer();
        b->bytes.assign(std::max(bytes, 1U), 0);
        return b;
    }
    static uint32_t release(Buffer* b)
    {
        if (!b) return 0;
        if (--b->refs == 0) {
            delete b;
            return 0;
        }
        return b->refs;
    }
    long make_surface(uint32_t w, uint32_t h, uint32_t format, render::GpuSurface** surface)
    {
        auto s = std::make_unique<Surface>();
        s->make(w, h, format);
        *surface = as<render::GpuSurface>(s.get());
        own_surfaces_.push_back(std::move(s));
        return 0;
    }

    Surface back_;
    std::vector<std::unique_ptr<Surface>> own_surfaces_;
};

class HeadlessGpuContext final : public render::NullGpuContext {
public:
    bool create_instance() override { return true; }
    bool adapter_ready() override { return true; }
    long create_device(const render::GpuDeviceRequest&) override
    {
        made_ = true;
        return 0;
    }
    bool device_made() const override { return made_; }
    void mark_ready() override { ready_ = true; }
    bool ready() const override { return ready_; }
    bool reset() override { return true; }

private:
    bool made_ = false;
    bool ready_ = false;
};

} // namespace

bool texture_level(render::GpuTexture* texture, uint32_t level, TextureLevel* out)
{
    auto* t = reinterpret_cast<Texture*>(texture);
    if (!t || level >= t->levels.size() || !out) return false;
    const Surface& s = *t->levels[level];
    *out = {s.width, s.height, s.format, s.pitch, s.bits.data(), s.bits.size(), static_cast<uint32_t>(t->levels.size())};
    return true;
}

render::Gpu& headless_gpu()
{
    static HeadlessGpu gpu;
    return gpu;
}

render::GpuContext& headless_gpu_context()
{
    static HeadlessGpuContext context;
    return context;
}

render::UiBackend& headless_ui_backend()
{
    static render::NullUiBackend backend;
    return backend;
}

audio::Device& null_audio_device()
{
    static audio::NullAudioDevice device;
    return device;
}

media::Movie& null_movie()
{
    static media::NullMovie movie;
    return movie;
}

net::Transport& null_transport()
{
    static net::NullTransport transport;
    return transport;
}

} // namespace headless
