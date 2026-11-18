//! THUG2's side of the engine contract ([`engine_core`]): what the app needs to know about THUG2 to host it, said
//! as the contract's plain data, over the engine itself (`thug2-sys`). No Bevy.
//!
//! It lives in thug2-headless beside `thug2-sys`; a host (skate3-quest-ar) depends on it by revision.
//!
//! - [`engine`]: the game as the runtime drives it ([`engine_core::EngineCore`]), made on a worker by its factory.
//! - [`hud`]: the game's 2D as a [`engine_core::hud::HudFrame`]: which draw is which part, and where each part goes.
//! - [`input`]: the Touch controllers as the game's pad, and its controls sheet.
//! - [`model`]: the skater's model as meshes and plain materials, and its pose (palette and draw matrix) in the
//!   contract's space.
//! - [`sound`]: the game's voices as the contract's, from the skater's board, and its sounds' samples.
//! - [`stage`]: the stage and park as the game's world (its units, face classes, rails; not mid-grind), putting the
//!   skater down, and where it is.
//! - [`DATA`]: the player's THUG2 files, as the app's installer finds and keeps them.
pub mod engine;
pub mod hud;
pub mod input;
pub mod model;
pub mod sound;
pub mod stage;

/// The player's THUG2 files: their installed PC game, the folder holding `Data` (skate3-quest-ar's `docs/THUG2.md`).
pub const DATA: engine_core::GameFiles = engine_core::GameFiles {
    drop: "THUG2",
    folder: "thug2",
    marker: "Data",
    beside: &[],
    how: &[
        "Copy your installed THUG2 PC game folder (the one holding Data),",
        "or a zip of it, to Download/THUG2 on this headset, then import",
        "it here. Over USB: scripts/quest/push-thug2.sh. Guide: docs/THUG2.md.",
    ],
};
