//! THUG2 as the runtime drives it ([`engine_core::EngineCore`]): the game (`thug2-sys`), booted on a worker
//! ([`factory`]: its scripts and an empty level, then a few frames for the skater to settle), stepped at its 60 Hz on
//! the pad the controllers make, given the stage and park as its world, and read after each step: the skater's pose
//! and where it is, its voices, its 2D.
//!
//! It waits for a stage of the runtime's before it steps (until then the skater is at the level's own start), and the
//! skater is in the runtime's world from the first step after.
use crate::{hud, input, model, sound, stage};
use engine_core::audio::{Clip, Voice};
use engine_core::hud::{HudFrame, Rgba8, TextureId};
use engine_core::skate::{SkateMode, SkatePark, SkaterLink};
use engine_core::{Applied, EngineCore, EngineFactory, Focus, Model, PadHeld, PlacePlayer, Pose, Stage, TouchState};
use std::path::PathBuf;
use std::time::Duration;
use thug2_sys as t2;

/// Game frames run at boot, before the skater is put anywhere (the level puts it at its own start first).
pub const SETTLE_FRAMES: u32 = 120;

/// THUG2 made on the runtime's worker from the player's game folder (the one holding `Data`), the files it makes
/// kept in `cache`.
pub fn factory(game: PathBuf, cache: PathBuf) -> EngineFactory {
    Box::new(move || {
        let started = std::time::Instant::now();
        let mut engine = t2::boot(&game, Some(&cache), &["-level", "Default"]).map_err(|e| e.to_string())?;
        let mut skater = None;
        for _ in 0..SETTLE_FRAMES {
            skater = engine.step(&t2::PAD_AT_REST).map_err(|e| e.to_string())?;
        }
        // (the skater's look, as the game built it)
        let model = engine.skater_model();
        eprintln!(
            "THUG2 ready in {:.0} ms (skater {}, model: {} meshes, {} textures)",
            started.elapsed().as_secs_f32() * 1000.0,
            skater.is_some(),
            model.as_ref().map_or(0, |m| m.meshes.len()),
            model.as_ref().map_or(0, |m| m.textures.len())
        );
        Ok(Box::new(Thug2::new(engine, skater, model)) as Box<dyn EngineCore>)
    })
}

/// The running game.
pub struct Thug2 {
    engine: t2::Engine,
    input: input::Input,
    /// What the game has of the stage.
    stage: stage::StageFeed,
    /// Where to put the skater, waiting for the game to have our world.
    spawn: Option<t2::Spawn>,
    current: Option<t2::Skater>,
    /// It has stepped since it was given our world (the skater is in it, not at the level's own start).
    placed: bool,
    /// The skater was put somewhere else (a teleport, our world taken with a place, the game's own placing): the next
    /// pose is a cut.
    cut: bool,
    /// The game's own count of the skater put somewhere outside its motion (a wall plant, a respawn, a turn-round).
    placements: u64,
    pose: t2::Pose,
    /// The last step left a pose.
    posed: bool,
    model: Option<t2::SkaterModel>,
    hud_on: bool,
    hud: t2::Hud,
    /// Stepped since its 2D was last read.
    stepped: bool,
    voices: Vec<t2::Voice>,
}

impl Thug2 {
    fn new(engine: t2::Engine, skater: Option<t2::Skater>, model: Option<t2::SkaterModel>) -> Self {
        Self {
            engine,
            input: input::Input::default(),
            stage: stage::StageFeed::default(),
            spawn: None,
            current: skater,
            placed: false,
            cut: false,
            placements: 0,
            pose: t2::Pose::default(),
            posed: false,
            model,
            hud_on: false,
            hud: t2::Hud::default(),
            stepped: false,
            voices: Vec::new(),
        }
    }

    /// The skater, once it is in our world.
    fn skater(&self) -> Option<&t2::Skater> {
        self.current.as_ref().filter(|_| self.placed)
    }
}

impl EngineCore for Thug2 {
    fn period(&self) -> Duration {
        model::PERIOD
    }

    fn set_input(&mut self, touch: &TouchState, held: PadHeld) {
        self.input.set_input(touch, held);
    }

    /// Given when it changes: not mid-grind, and once there's somewhere to put the skater.
    fn set_stage(&mut self, stage: &Stage) -> Applied {
        let given = self.stage.given();
        let placing = self.spawn.is_some();
        let applied = self.stage.set_stage(Some(&mut self.engine), self.current.as_ref(), stage, &mut self.spawn);
        // (the skater put down with the world: a cut)
        self.cut |= placing && self.spawn.is_none();
        if self.stage.given() != given {
            eprintln!("THUG2 world: {} faces, {} rails (AR revision {})", self.stage.faces, self.stage.rails, stage.revision);
        }
        applied
    }

    /// On the board, a little over the point: now if the game has a stage of ours, else with the next it takes.
    fn place_player(&mut self, place: PlacePlayer) {
        let spawn = stage::spawn(place.point, place.forward);
        if self.stage.given().is_some() {
            self.engine.teleport(spawn);
            self.cut = true;
        } else {
            self.spawn = Some(spawn);
        }
    }

    fn wants_steps(&self) -> bool {
        self.stage.given().is_some()
    }

    fn step(&mut self) -> Result<(), String> {
        self.current = self.engine.step(&self.input.pad).map_err(|e| e.to_string())?;
        // (the game placed the skater itself this step - a wall plant, a respawn: a cut. Before the pose is read, so a
        // step with none keeps it for the next pose)
        let placements = self.engine.skater_placements();
        self.cut |= placements != self.placements;
        self.placements = placements;
        self.posed = self.engine.skater_pose(&mut self.pose);
        // (the world and the skater's place are taken at the start of a step: from now it is in ours)
        self.placed = true;
        self.stepped = true;
        self.engine.voices(&mut self.voices);
        Ok(())
    }

    fn focus(&self) -> Option<Focus> {
        self.skater().and_then(|s| stage::link(s).focus())
    }

    fn models(&mut self) -> Vec<Model> {
        self.model.take().map(|m| model::source(&m)).into_iter().collect()
    }

    fn poses(&mut self, out: &mut Vec<Pose>) {
        if !std::mem::take(&mut self.posed) {
            return;
        }
        let mut pose = Pose { model: model::SKATER, discontinuity: std::mem::take(&mut self.cut), ..Default::default() };
        model::pose(&self.pose, &mut pose);
        out.push(pose);
    }

    fn voices(&mut self, out: &mut Vec<Voice>) {
        let at = self.current.as_ref().and_then(sound::sound_at).unwrap_or_default();
        out.extend(self.voices.iter().map(|v| sound::voice(v, at)));
    }

    fn clip(&mut self, id: u32) -> Option<Clip> {
        self.engine.sound(id).map(sound::clip)
    }

    /// The game takes its 2D down only while it's seen.
    fn want_hud(&mut self, wanted: bool) {
        if self.hud_on != wanted {
            self.engine.hud_enable(wanted);
            self.hud_on = wanted;
            self.hud = t2::Hud::default();
        }
    }

    /// The frame's last step's 2D (the balance meter goes at the skater).
    fn hud(&mut self, out: &mut HudFrame) -> bool {
        if self.hud_on && std::mem::take(&mut self.stepped) {
            self.engine.hud(&mut self.hud);
        }
        self.stepped = false;
        let balancing = self.current.is_some_and(|s| s.balance.is_some());
        hud::frame(&self.hud, balancing, out);
        true
    }

    fn hud_texture(&mut self, id: TextureId) -> Option<Rgba8> {
        self.engine.hud_texture(hud::game_texture(id)).map(hud::texture)
    }

    fn skate(&mut self) -> Option<&mut dyn SkateMode> {
        Some(self)
    }
}

impl SkateMode for Thug2 {
    fn set_park(&mut self, park: &SkatePark) -> Applied {
        self.stage.set_park(park)
    }

    fn link(&self) -> Option<SkaterLink> {
        self.skater().map(stage::link)
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_moves_to_the_runtimes_worker() {
        fn send<T: Send>() {}
        send::<super::Thug2>();
    }
}
