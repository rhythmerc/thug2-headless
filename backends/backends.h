// The harness's backends: what app/ installs on Windows (Win32 files, Winsock, DirectSound, FFmpeg, Direct3D 9),
// in their headless Linux forms.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fs { class Disk; }
namespace render { class Gpu; class GpuContext; class UiBackend; }
namespace audio { class Device; }
namespace media { class Movie; }
namespace net { class Transport; }
namespace input { class Source; }

namespace render { struct GpuTexture; struct GpuStateBlock; }

namespace headless {

// A texture's level as the in-memory GPU holds it (format: Direct3D 9's number, or a DXTn fourcc).
struct TextureLevel {
    uint32_t width, height, format, pitch;
    const uint8_t* bits;
    size_t bytes;
    uint32_t levels;
};
bool texture_level(render::GpuTexture* texture, uint32_t level, TextureLevel* out);

fs::Disk& posix_disk();
// Where the disk keeps the files it makes (Default's node file); null or "": $XDG_CACHE_HOME/thug2-headless.
void set_cache_folder(const char* folder);
const std::string& cache_folder_path();
// The game's folder (absolute): relative paths the game opens are in it (the process's working folder is left alone).
void set_game_folder(const char* folder);
render::Gpu& headless_gpu();
render::GpuContext& headless_gpu_context();
render::UiBackend& headless_ui_backend();
audio::Device& null_audio_device();
// The sound output a host plays (capture_audio.cpp), and its clock: a game frame has passed.
audio::Device& capture_audio_device();
void sound_frame_done();
// The game's 2D for a host (capture_hud.cpp): taken down at the end of each frame when asked for (in the game's
// thread), the GPU passing it each 2D draw while capturing, and told of a texture released.
void hud_frame_done();
bool hud_capturing();
void hud_record(const render::GpuStateBlock& state, uint32_t primitive, const void* indices, uint32_t index_format,
                uint32_t index_count, const void* vertices, uint32_t vertex_count, uint32_t stride);
void hud_texture_released(render::GpuTexture* texture);
media::Movie& null_movie();
net::Transport& null_transport();
input::Source& scripted_input();

} // namespace headless
