// The harness's backends: what app/ installs on Windows (Win32 files, Winsock, DirectSound, FFmpeg, Direct3D 9),
// in their headless Linux forms.
#pragma once

#include <string>

namespace fs { class Disk; }
namespace render { class Gpu; class GpuContext; class UiBackend; }
namespace audio { class Device; }
namespace media { class Movie; }
namespace net { class Transport; }
namespace input { class Source; }

namespace headless {

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
media::Movie& null_movie();
net::Transport& null_transport();
input::Source& scripted_input();

} // namespace headless
