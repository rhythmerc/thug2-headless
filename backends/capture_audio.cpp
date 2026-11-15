// The sound output a host plays: an audio::Device that plays nothing itself but keeps what the game asks of it - each
// buffer's samples, and each voice's state (playing, looping, its volume, pan and frequency) -
// for the host to read between steps (app/thug2_engine.h, thug2_sound_*).
//
// The game decodes its sounds (ADPCM .snd files) to 16-bit PCM before it makes a buffer (engine/sound/sfx_voices.cpp
// read_in), so a buffer's bytes are 16-bit samples, interleaved by its channel count. A sound is kept once, keyed by
// its samples: the game makes a buffer each time a voice plays another wave, and fills it from the wave's one copy.
//
// The output has no 3D buffers (get_caps), so every voice is a stereo one: the game sets its volume (the sound's own,
// with its fall-off from the camera), pan and frequency. The buffers' play positions advance with the game's
// frames (a frame a step, at the game's 60 a second): `buffer_finished` is when a buffer played once has run out.
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "app/thug2_engine.h"
#include "backends/backends.h"
#include "engine/audio/audio_device.h"

namespace {

constexpr double kFrameSeconds = 1.0 / 60.0;

struct Sound {
    uint16_t channels{};
    uint32_t rate{};
    std::vector<uint8_t> bytes;
};

struct Buffer {
    uint32_t id{};
    uint32_t sound{};  // 0: not filled yet
    uint16_t channels{};
    uint32_t rate{};   // its own (the wave's)
    uint32_t frequency{};
    uint32_t bytes{};
    bool playing{};
    bool looping{};
    uint32_t starts{};  // played from the start this many times
    double played{};   // seconds of its own samples played since it started (at its frequency)
    int32_t volume{};   // hundredths of a decibel
    int32_t pan{};
};

struct Store {
    std::map<uint64_t, uint32_t> by_key; // a sound's samples' hash -> its id
    std::map<uint32_t, Sound> sounds;
    std::map<uint32_t, std::unique_ptr<Buffer>> buffers;
    uint32_t next_buffer = 1;
    uint32_t next_sound = 1;
};

Store& store()
{
    static Store s;
    return s;
}

uint64_t key_of(std::span<const uint8_t> bytes, uint16_t channels, uint32_t rate)
{
    uint64_t h = 0xcbf29ce484222325ULL ^ (uint64_t{channels} << 32 | rate);
    for (uint8_t b : bytes) h = (h ^ b) * 0x100000001b3ULL;
    return h ^ bytes.size();
}

Buffer* buffer_of(audio::SoundBuffer* buffer) { return reinterpret_cast<Buffer*>(buffer); }

// How long a buffer runs, in seconds of its own samples (at its own rate).
double length_of(const Buffer& b)
{
    const uint32_t frame_bytes = 2U * (b.channels ? b.channels : 1U);
    return b.rate ? static_cast<double>(b.bytes / frame_bytes) / b.rate : 0.0;
}

class CaptureAudioDevice final : public audio::Device {
public:
    audio::SoundOutput* open(void*) override { return reinterpret_cast<audio::SoundOutput*>(&output_); }
    void close(audio::SoundOutput*) override {}
    void list_outputs(audio::OutputListed listed) override
    {
        if (listed) listed("thug2-headless host output");
    }
    bool get_caps(audio::SoundOutput*, audio::OutputCaps& caps) override
    {
        // (no 3D: the game's 3D voices get only its master volume, and keep their frequency - apply_voice_settings
        // - where its stereo ones get each sound's volume, its fall-off and its pitch; the host places them)
        caps.free_3d_buffers = 0;
        caps.free_mixing_buffers = 64;
        return true;
    }
    audio::SoundListener* create_listener(audio::SoundOutput*) override { return reinterpret_cast<audio::SoundListener*>(&listener_); }
    void orient_listener(audio::SoundListener*, audio::Point3) override {}
    void commit_listener(audio::SoundListener*) override {}

    audio::MadeBuffer create_buffer(audio::SoundOutput*, const audio::BufferDesc& desc) override
    {
        Store& s = store();
        auto b = std::make_unique<Buffer>();
        b->id = s.next_buffer++;
        b->channels = desc.format.channels;
        b->rate = desc.format.samples_per_second_;
        b->frequency = b->rate;
        b->bytes = desc.bytes;
        Buffer* raw = b.get();
        s.buffers[raw->id] = std::move(b);
        return {reinterpret_cast<audio::SoundBuffer*>(raw), true};
    }
    audio::SoundBuffer3d* buffer_3d(audio::SoundBuffer*) override { return nullptr; }
    void fill_buffer(audio::SoundBuffer* buffer, std::span<const uint8_t> samples) override
    {
        Buffer* b = buffer_of(buffer);
        if (!b) return;
        Store& s = store();
        const uint64_t key = key_of(samples, b->channels, b->rate);
        auto at = s.by_key.find(key);
        if (at == s.by_key.end()) {
            const uint32_t id = s.next_sound++;
            s.sounds[id] = Sound{b->channels, b->rate, std::vector<uint8_t>(samples.begin(), samples.end())};
            at = s.by_key.emplace(key, id).first;
        }
        b->sound = at->second;
        b->bytes = static_cast<uint32_t>(samples.size());
    }
    void release_buffer(audio::SoundBuffer* buffer) override
    {
        if (Buffer* b = buffer_of(buffer)) store().buffers.erase(b->id);
    }
    void release_buffer_3d(audio::SoundBuffer3d*) override {}
    bool play_buffer(audio::SoundBuffer* buffer, bool looping) override
    {
        Buffer* b = buffer_of(buffer);
        if (!b) return false;
        b->playing = true;
        b->looping = looping;
        b->played = 0.0;
        ++b->starts;
        return true;
    }
    void stop_buffer(audio::SoundBuffer* buffer) override
    {
        if (Buffer* b = buffer_of(buffer)) b->playing = false;
    }
    bool buffer_finished(audio::SoundBuffer* buffer) override
    {
        const Buffer* b = buffer_of(buffer);
        return !b || !b->playing;
    }
    void set_buffer_volume(audio::SoundBuffer* buffer, int attenuation) override
    {
        if (Buffer* b = buffer_of(buffer)) b->volume = attenuation;
    }
    void set_buffer_pan(audio::SoundBuffer* buffer, int pan) override
    {
        if (Buffer* b = buffer_of(buffer)) b->pan = pan;
    }
    void set_buffer_frequency(audio::SoundBuffer* buffer, uint32_t frequency) override
    {
        if (Buffer* b = buffer_of(buffer)) b->frequency = frequency;
    }
    void place_buffer_3d(audio::SoundBuffer3d*, audio::BufferRange, audio::Point3) override {}
    // (streams and movies: no codec plays here)
    bool buffer_position(audio::SoundBuffer*, uint32_t& position) override
    {
        position = 0;
        return false;
    }
    void write_buffer(audio::SoundBuffer*, uint32_t, std::span<const uint8_t>) override {}
    bool resume_buffer(audio::SoundBuffer*) override { return false; }
    int track_volume(int volume) override { return volume; }

private:
    int output_ = 0;
    int listener_ = 0;
};

} // namespace

namespace headless {

audio::Device& capture_audio_device()
{
    static CaptureAudioDevice device;
    return device;
}

// A game frame has passed: buffers played once run out.
void sound_frame_done()
{
    for (auto& [id, b] : store().buffers) {
        if (!b->playing) continue;
        // (played faster or slower than its own rate, it runs through its samples that much faster or slower)
        const uint32_t frequency = b->frequency ? b->frequency : b->rate;
        b->played += kFrameSeconds * (b->rate ? static_cast<double>(frequency) / b->rate : 1.0);
        if (!b->looping && b->played >= length_of(*b)) b->playing = false;
    }
}

} // namespace headless

extern "C" uint32_t thug2_sound_voices(Thug2Voice* out, uint32_t max)
{
    uint32_t n = 0;
    for (const auto& [id, b] : store().buffers) {
        if (!b->playing || b->sound == 0) continue;
        if (out && n < max) {
            Thug2Voice& v = out[n];
            v = {};
            v.voice = b->id;
            v.sound = b->sound;
            v.starts = b->starts;
            v.looping = b->looping ? 1 : 0;
            v.volume = b->volume;
            v.pan = b->pan;
            v.frequency = b->frequency ? b->frequency : b->rate;
            v.rate = b->rate;
        }
        ++n;
    }
    return n;
}

extern "C" int thug2_sound(uint32_t sound, Thug2Sound* out, int16_t* samples, uint32_t count)
{
    const auto at = store().sounds.find(sound);
    if (at == store().sounds.end()) return 1;
    const Sound& s = at->second;
    const uint32_t frames_samples = static_cast<uint32_t>(s.bytes.size() / 2);
    if (out) *out = {s.channels, 0, s.rate, frames_samples};
    if (samples && count >= frames_samples) std::memcpy(samples, s.bytes.data(), size_t{frames_samples} * 2);
    return 0;
}
