//! THUG2's sound: the game's voices (`thug2_sys::Voice`, after each step) as the contract's
//! ([`engine_core::audio::Voice`]), heard from the skater's board, and its sounds' samples as clips.
//!
//! The game's own fall-off from its camera stays in each voice's gain; its pan (for its camera) is left out: the
//! runtime places the voice in the room.
use crate::stage::{SOUND_HEIGHT, link};
use engine_core::audio::{Clip, Voice};
use engine_core::glam::Vec3;
use thug2_sys as t2;

/// Where the skater's sounds come from: its board, a little up from its feet.
pub fn sound_at(skater: &t2::Skater) -> Option<Vec3> {
    let focus = link(skater).focus()?;
    Some(focus.feet + focus.up * SOUND_HEIGHT)
}

/// A voice the game is playing, heard from `at`.
pub fn voice(v: &t2::Voice, at: Vec3) -> Voice {
    Voice { id: v.voice, clip: v.sound, starts: v.starts, looping: v.looping, gain: v.gain, speed: v.speed, at }
}

/// A sound's samples (`thug2_sys::Engine::sound`).
pub fn clip(sound: t2::Sound) -> Clip {
    Clip { channels: sound.channels, rate: sound.rate, samples: sound.samples }
}
