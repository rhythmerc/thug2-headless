//! The skater and the room's walls with park pieces against them, on the real engine (`THUG2_GAME_DIR`: the folder
//! holding the player's `Data`; skipped without it). One test (the engine boots once a process):
//! - riding a wall as ground: up a quarter pipe backed against a room wall with up held (the user's report), and off a
//!   kicker into the wall for comparison; the skater must never ride the wall as if it were floor (upright across it,
//!   not in a wall ride);
//! - a wall leaning a little (as scanned walls are): rolled and pushed into fast, never ridden as floor nor gone
//!   through;
//! - walls overhanging the room 1-10°, rolled and jumped into, square-on and turned, near the origin and far out: the
//!   skater stays on the room's side;
//! - wall rides: a glancing jump at the wall with triangle held starts them (the game's own rule).
//!
//! With walls fed skatable (SKATABLE | WALL, before this test), the game took them as floor: the quarter pipe put
//! the skater on the wall above it, and wall rides never started (the game reads a skatable face as no wall).
use engine_core::glam::{Quat, Vec2, Vec3};
use engine_core::skate::{ParkPiece, SkatePark};
use engine_core::{Solid, Stage};
use std::f32::consts::FRAC_PI_2;
use std::path::Path;
use thug2_engine::stage;
use thug2_sys::{self as t2, PAD_AT_REST, Pad, State, buttons};

/// Where the room's wall is (z, metres), the room on its near side (-z).
const WALL_AT: f32 = 3.0;
/// Up held on the stick: the push.
const UP: Pad = Pad { left_y: 0, ..PAD_AT_REST };

#[test]
fn walls_are_walls() {
    let Some(dir) = std::env::var_os("THUG2_GAME_DIR") else {
        eprintln!("THUG2_GAME_DIR not set: skipped");
        return;
    };
    let cache = std::env::temp_dir().join("thug2-engine-test-cache");
    let mut engine = t2::boot(Path::new(&dir), Some(&cache), &["-level", "Default"]).expect("boot");
    for _ in 0..120 {
        engine.step(&PAD_AT_REST).expect("step");
    }
    // (`WALLS_ONLY`: one part - pieces, leaning, overhangs or wallrides - to trace it)
    let mut problems = Vec::new();
    let only = std::env::var("WALLS_ONLY").ok();
    let wanted = |name: &str| only.as_deref().is_none_or(|o| o == name);
    if wanted("pieces") {
        problems.extend(pieces_at_the_wall(&mut engine));
    }
    if wanted("leaning") {
        problems.extend(leaning_walls(&mut engine));
    }
    if wanted("overhangs") {
        problems.extend(overhangs(&mut engine));
    }
    if wanted("wallrides") {
        problems.extend(wall_rides(&mut engine));
    }
    assert!(problems.is_empty(), "{problems:#?}");
}

/// A floor and a wall across +z at [`WALL_AT`], leaning `lean` degrees (its top further out: positive), 4 m tall, as
/// the room's Scene planes come (both windings).
fn room(lean: f32) -> Vec<[Vec3; 4]> {
    let floor = [Vec3::new(-40.0, 0.0, -40.0), Vec3::new(-40.0, 0.0, 40.0), Vec3::new(40.0, 0.0, 40.0), Vec3::new(40.0, 0.0, -40.0)];
    let top = WALL_AT + 4.0 * lean.to_radians().tan();
    let wall = [Vec3::new(-30.0, 0.0, WALL_AT), Vec3::new(30.0, 0.0, WALL_AT), Vec3::new(30.0, 4.0, top), Vec3::new(-30.0, 4.0, top)];
    let back = [wall[0], wall[3], wall[2], wall[1]];
    vec![floor, wall, back]
}

/// The app's extrusion (skate-game xr/build/pieces.rs `extrude`): a height field's top, its end caps and back walls.
fn extrude(profile: &[Vec2], width: f32) -> Vec<[Vec3; 4]> {
    let x = width * 0.5;
    let at = |x: f32, p: Vec2| Vec3::new(x, p.y, p.x);
    let mut quads = Vec::new();
    for pair in profile.windows(2) {
        let (p, q) = (pair[0], pair[1]);
        quads.push([at(-x, p), at(x, p), at(x, q), at(-x, q)]);
        for side in [-x, x] {
            quads.push([Vec3::new(side, 0.0, p.x), Vec3::new(side, 0.0, q.x), at(side, q), at(side, p)]);
        }
    }
    let (first, last) = (profile[0], profile[profile.len() - 1]);
    quads.push([at(-x, first), at(x, first), at(x, first.with_y(0.0)), at(-x, first.with_y(0.0))]);
    quads.push([at(-x, last), at(x, last), at(x, last.with_y(0.0)), at(-x, last.with_y(0.0))]);
    quads
}

#[derive(Clone, Copy, Debug)]
enum Kind {
    QuarterPipe,
    Kicker,
}

/// The app's quarter pipe or kicker of size `s` (pieces.rs `geometry`), turned to face -z (its ramp towards the room,
/// its back to the wall), its back (the quarter pipe's) or its lip (the kicker's) `gap` short of the wall.
fn piece(kind: Kind, s: f32, gap: f32) -> Vec<[Vec3; 4]> {
    let (profile, back) = match kind {
        Kind::QuarterPipe => {
            let (radius, deck) = (2.0 * s, 1.0);
            let z0 = -(radius + deck) * 0.5;
            let mut profile = vec![Vec2::new(z0, radius)];
            let lip = z0 + deck;
            profile.extend((0..=16).map(|i| {
                let a = FRAC_PI_2 * i as f32 / 16.0;
                Vec2::new(lip + radius * (1.0 - a.cos()), radius * (1.0 - a.sin()))
            }));
            (profile, z0)
        }
        Kind::Kicker => {
            let (length, height) = (2.5 * s, 0.8 * s);
            let profile: Vec<Vec2> = (0..=16)
                .map(|i| {
                    let t = i as f32 / 16.0;
                    Vec2::new(-length * 0.5 + length * t, height * (1.0 - t) * (1.0 - t))
                })
                .collect();
            (profile, -length * 0.5)
        }
    };
    // (turned half round: its back towards +z, at the wall less the gap)
    let shift = WALL_AT - gap + back;
    extrude(&profile, 4.0 * s.max(0.5)).into_iter().map(|q| q.map(|p| Vec3::new(-p.x, p.y, shift - p.z))).collect()
}

fn faces(room_quads: Vec<[Vec3; 4]>, piece: Vec<[Vec3; 4]>) -> Vec<t2::Triangle> {
    let stage = Stage { revision: 1, room_quads, solids: vec![Solid { id: 1, quads: piece }], ..Default::default() };
    let park = SkatePark { revision: 1, pieces: vec![ParkPiece { solid: 1, ..Default::default() }], ..Default::default() };
    as_fed(stage::triangles(&stage, &park))
}

/// The faces as the stage gives them; with `WALLS_SKATABLE` set, the walls skatable too, as they went before (to
/// trace what that did).
fn as_fed(mut faces: Vec<t2::Triangle>) -> Vec<t2::Triangle> {
    if std::env::var_os("WALLS_SKATABLE").is_some() {
        for t in faces.iter_mut().filter(|t| t.flags & t2::face::WALL != 0) {
            t.flags |= t2::face::SKATABLE;
        }
    }
    faces
}

/// How far past the wall's plane at the skater's height (negative: in the room), metres.
fn past(s: &t2::Skater, lean: f32) -> f32 {
    let p = stage::from_game(s.position);
    p.z - (WALL_AT + p.y * lean.to_radians().tan())
}

/// It rides the wall as floor: on the ground (to the game) at the wall, upright across it (its up level). (In the air
/// its up can lie level for a while by the game's own rule: a landing feeler meeting a face it can't land on turns
/// the skater along it, core_physics.cpp land_on_hit - a kicker launched into the wall from close does that.)
fn on_wall_as_floor(s: &t2::Skater, lean: f32) -> bool {
    s.state == State::Ground && past(s, lean) > -0.3 && s.up[1].abs() < 0.5
}

/// What a run did at the wall.
#[derive(Default)]
struct Run {
    /// Frames ridden as floor (see [`on_wall_as_floor`]), the longest stretch of them, how high up it got (metres).
    as_floor: u32,
    longest: u32,
    top: f32,
    /// Frames in a wall ride.
    wall_ride: u32,
    /// Frames on the ground behind the wall's plane (more than 5 cm).
    behind: u32,
    /// The states it went through (each change once).
    states: Vec<State>,
    /// After an ollie on the wall (when it was ridden as floor): the state changes in the next second.
    ollie_changes: Option<usize>,
}

impl Run {
    fn note(&mut self, s: &t2::Skater, lean: f32, streak: &mut u32) {
        if std::env::var_os("WALLS_DEBUG").is_some() && past(s, lean) > -1.5 {
            let p = stage::from_game(s.position);
            eprintln!("  {:?} at {p:.2} past {:+.3} up {:.2?} v {:.0?}", s.state, past(s, lean), s.up, s.velocity);
        }
        if on_wall_as_floor(s, lean) {
            self.as_floor += 1;
            *streak += 1;
            self.longest = self.longest.max(*streak);
            self.top = self.top.max(stage::from_game(s.position).y);
        } else {
            *streak = 0;
        }
        if s.state == State::Wall {
            self.wall_ride += 1;
        }
        if s.state == State::Ground && past(s, lean) > 0.05 {
            self.behind += 1;
        }
        if self.states.last() != Some(&s.state) {
            self.states.push(s.state);
        }
    }
}

/// At the piece from 9 m out with up held, for 6 seconds (`release`: the stick let go once it is first in the air).
/// On the wall as floor for 15 frames: an ollie (the user's "spaz").
fn run_at_piece(engine: &mut t2::Engine, faces: &[t2::Triangle], side: f32, release: bool) -> Run {
    let start = Vec3::new(side, 0.0, WALL_AT - 9.0);
    engine.set_world(faces, Some(stage::spawn(start, Vec3::Z)));
    let mut last = engine.step(&PAD_AT_REST).unwrap().unwrap();
    let mut run = Run::default();
    let mut streak = 0;
    let mut ollie: Option<(u32, Vec<State>)> = None;
    let mut flown = false;
    for frame in 0..360u32 {
        flown |= last.state == State::Air;
        let mut pad = if release && flown { PAD_AT_REST } else { UP };
        if ollie.is_none() && streak >= 15 {
            ollie = Some((frame, vec![last.state]));
        }
        if let Some((at, _)) = &ollie {
            if frame - at < 3 {
                pad.buttons = buttons::CROSS;
            }
        }
        last = engine.step(&pad).unwrap().unwrap();
        run.note(&last, 0.0, &mut streak);
        if let Some((at, states)) = ollie.as_mut() {
            if frame - *at < 60 && states.last() != Some(&last.state) {
                states.push(last.state);
            }
        }
    }
    run.ollie_changes = ollie.map(|(_, s)| s.len() - 1);
    run
}

fn pieces_at_the_wall(engine: &mut t2::Engine) -> Vec<String> {
    let mut problems = Vec::new();
    for (kind, gaps) in [(Kind::QuarterPipe, [0.0f32, 0.05, 0.3]), (Kind::Kicker, [0.5, 1.0, 2.0])] {
        for s in [0.5f32, 0.75, 1.0] {
            for gap in gaps {
                let faces = faces(room(0.0), piece(kind, s, gap));
                // (three places across it with up held all the way, then the same letting go of the stick in the air)
                let runs: Vec<Run> = [false, true]
                    .iter()
                    .flat_map(|&release| [-0.4f32, 0.0, 0.3].map(|side| (side, release)))
                    .map(|(side, release)| run_at_piece(engine, &faces, side, release))
                    .collect();
                let as_floor: Vec<u32> = runs.iter().map(|r| r.as_floor).collect();
                let longest: Vec<u32> = runs.iter().map(|r| r.longest).collect();
                let top: Vec<String> = runs.iter().map(|r| format!("{:.2}", r.top)).collect();
                let ollies: Vec<Option<usize>> = runs.iter().map(|r| r.ollie_changes).collect();
                let mut states: Vec<State> = runs.iter().flat_map(|r| r.states.clone()).collect();
                states.sort_by_key(|s| *s as u8);
                states.dedup();
                eprintln!(
                    "PIECE {kind:?} size {s} gap {gap}: frames on the wall as floor {as_floor:?} (longest {longest:?}, up to m {top:?}); ollie there: state changes {ollies:?}; {states:?}; behind the wall {:?}",
                    runs.iter().map(|r| r.behind).collect::<Vec<_>>()
                );
                if runs.iter().any(|r| r.behind > 0) {
                    problems.push(format!("{kind:?} size {s} gap {gap}: behind the wall"));
                }
                if runs.iter().any(|r| r.longest > 3) {
                    problems.push(format!("{kind:?} size {s} gap {gap}: the wall ridden as floor {longest:?} frames"));
                }
            }
        }
    }
    problems
}

/// The room turned `turn` degrees about y and moved to `centre` (contract metres), and back.
#[derive(Clone, Copy)]
struct Placed {
    turn: f32,
    rotation: Quat,
    centre: Vec3,
}

impl Placed {
    fn new(turn: f32, centre: Vec3) -> Self {
        Self { turn, rotation: Quat::from_rotation_y(turn.to_radians()), centre }
    }

    fn place(&self, p: Vec3) -> Vec3 {
        self.rotation * p + self.centre
    }

    /// The skater in the room's own frame.
    fn local(&self, s: t2::Skater) -> t2::Skater {
        t2::Skater {
            position: stage::to_game(self.rotation.inverse() * (stage::from_game(s.position) - self.centre)),
            up: (self.rotation.inverse() * Vec3::from_array(s.up)).to_array(),
            velocity: (self.rotation.inverse() * Vec3::from_array(s.velocity)).to_array(),
            ..s
        }
    }
}

/// Rolled (pushed, up held) or jumped (`jump`) into the wall leaning `lean` (negative: overhanging the room), heading
/// `degrees` off square-on, down a ramp for speed, the room `at` its place; 300 frames. Also how far past the wall's
/// plane the skater got at all (metres), in the air or not.
fn leaning_run(engine: &mut t2::Engine, lean: f32, degrees: f32, at: Placed, jump: bool) -> (Run, f32) {
    let heading = Vec3::new(degrees.to_radians().sin(), 0.0, degrees.to_radians().cos());
    let side = Vec3::Y.cross(heading);
    // A 1.5 m ramp (25°) down to 2 m short of the wall, a deck on top.
    let meet = Vec3::new(0.0, 0.0, WALL_AT);
    let foot = meet - heading * (2.0 / degrees.to_radians().cos());
    let top = foot - heading * (1.5 / 25f32.to_radians().tan()) + Vec3::Y * 1.5;
    let back = top - heading * 1.5;
    let w = side * 1.5;
    let mut quads = room(lean);
    quads.push([foot - w, foot + w, top + w, top - w]);
    quads.push([top - w, top + w, back + w, back - w]);
    let quads = quads.into_iter().map(|q| q.map(|p| at.place(p))).collect();
    let stage = Stage { revision: 1, room_quads: quads, ..Default::default() };
    let faces = as_fed(stage::triangles(&stage, &SkatePark::default()));
    if std::env::var_os("WALLS_DEBUG").is_some() {
        eprintln!(" run: lean {lean} heading {degrees} room turned {}° at {} jump {jump}", at.turn, at.centre);
    }
    engine.set_world(&faces, Some(stage::spawn(at.place(back + heading * 0.5), at.rotation * heading)));
    let mut last = at.local(engine.step(&PAD_AT_REST).unwrap().unwrap());
    let (mut run, mut streak, mut crouch, mut most) = (Run::default(), 0, 0, f32::MIN);
    for _ in 0..300 {
        let mut pad = UP;
        let to_wall = -past(&last, lean) / degrees.to_radians().cos();
        if jump && crouch < 3 && last.state == State::Ground && to_wall < 1.2 {
            crouch += 1;
            if crouch < 3 {
                pad.buttons = buttons::CROSS;
            }
        }
        last = at.local(engine.step(&pad).unwrap().unwrap());
        run.note(&last, lean, &mut streak);
        most = most.max(past(&last, lean));
    }
    (run, most)
}

/// Rolled (pushed) and jumped into a wall leaning a little, straight on and glancing, down a ramp for speed: never
/// ridden as floor, never through it.
fn leaning_walls(engine: &mut t2::Engine) -> Vec<String> {
    let mut problems = Vec::new();
    for lean in [-3.0f32, -1.0, 1.0, 3.0] {
        for degrees in [0.0f32, 30.0, 60.0] {
            let (mut longest, mut behind) = (Vec::new(), Vec::new());
            for jump in [false, true] {
                let (run, _) = leaning_run(engine, lean, degrees, Placed::new(0.0, Vec3::ZERO), jump);
                longest.push(run.longest);
                behind.push(run.behind);
            }
            eprintln!(
                "LEAN {lean}° heading {degrees}°: longest on the wall as floor (rolled, jumped) {longest:?}; frames on the ground behind it {behind:?}"
            );
            if longest.iter().any(|&l| l > 3) {
                problems.push(format!("wall leaning {lean}° hit at {degrees}°: ridden as floor {longest:?} frames"));
            }
            if behind.iter().any(|&b| b > 0) {
                problems.push(format!("wall leaning {lean}° hit at {degrees}°: behind it {behind:?} frames"));
            }
        }
    }
    problems
}

/// Walls overhanging the room 1-10° (as scanned walls can), rolled and jumped into square-on and at 30°, the room as
/// it is, turned 25°, and turned 137° far out (where the game's floats are coarser): the skater stays on the room's
/// side. (Jumped square-on into a 3-5° overhang it ended behind the wall before thug2-decomp's `overhang` fix: the
/// game's keep-above-ground feeler, starting 8 inches over the feet, came down onto the wall's back face under the
/// overhang and lifted the skater onto the wall's plane; it rose through the wall and landed on the back face.)
fn overhangs(engine: &mut t2::Engine) -> Vec<String> {
    let mut problems = Vec::new();
    let places = [Placed::new(0.0, Vec3::ZERO), Placed::new(25.0, Vec3::ZERO), Placed::new(137.0, Vec3::new(560.0, 0.0, 560.0))];
    for at in places {
        for degrees in [0.0f32, 30.0] {
            let (mut behind, mut furthest) = (Vec::new(), Vec::new());
            for overhang in 1..=10 {
                let lean = -(overhang as f32);
                for jump in [false, true] {
                    let (run, most) = leaning_run(engine, lean, degrees, at, jump);
                    if run.behind > 0 || most > 0.05 || run.longest > 3 {
                        problems.push(format!(
                            "overhang {overhang}° heading {degrees}°, room turned {}° at {}, jump {jump}: {} frames behind, furthest past m {most:+.3}, on it as floor {} frames",
                            at.turn, at.centre, run.behind, run.longest
                        ));
                    }
                    behind.push(run.behind);
                    furthest.push(format!("{most:+.3}"));
                }
            }
            eprintln!(
                "OVERHANG 1..10° heading {degrees}°, room turned {}° at {} (rolled, jumped each): frames on the ground behind it {behind:?}; furthest past it m {furthest:?}",
                at.turn, at.centre
            );
        }
    }
    problems
}

/// Glancing jumps at the wall with triangle held from the ollie on: the wall rides started, and ended (let run out,
/// or jumped out of with an ollie mid-ride) on the room's side of the wall. A wall across the room, one turned, and
/// one far out (where the game's floats are coarser).
fn wall_rides(engine: &mut t2::Engine) -> Vec<String> {
    let mut problems = Vec::new();
    let (mut total, mut tries) = (0, 0);
    for (turn, centre) in [(0.0f32, Vec3::ZERO), (25.0, Vec3::ZERO), (137.0, Vec3::new(560.0, 0.0, 560.0))] {
        // The room turned `turn` about y and moved to `centre`; and the skater back in the room's own frame.
        let placed = Placed::new(turn, centre);
        let (rotation, place, local) = (placed.rotation, |p| placed.place(p), |s| placed.local(s));
        let quads: Vec<[Vec3; 4]> = room(0.0).into_iter().map(|q| q.map(place)).collect();
        let faces = faces(quads, Vec::new());
        for jump_out in [false, true] {
            for degrees in [10.0f32, 20.0, 30.0, 45.0] {
                let heading = Vec3::new(degrees.to_radians().cos(), 0.0, degrees.to_radians().sin());
                let (mut rides, mut furthest) = (Vec::new(), Vec::new());
                for at in 0..4 {
                    // From far enough out to be pushed up to speed, meeting the wall at x about 0.
                    let back = 10.0 + at as f32 * 0.37;
                    let start = Vec3::new(0.0, 0.0, WALL_AT) - heading * back;
                    engine.set_world(&faces, Some(stage::spawn(place(start), rotation * heading)));
                    let mut last = local(engine.step(&PAD_AT_REST).unwrap().unwrap());
                    let (mut run, mut streak, mut crouch) = (Run::default(), 0, 0);
                    let (mut most, mut ollie_at) = (f32::MIN, None);
                    for frame in 0..360u32 {
                        let mut pad = UP;
                        let to_wall = -past(&last, 0.0) / degrees.to_radians().sin();
                        let [vx, _, vz] = last.velocity;
                        let speed = (vx * vx + vz * vz).sqrt() / t2::INCHES_PER_METRE;
                        if crouch < 3 && last.state == State::Ground && to_wall < speed * 0.3 + 0.2 {
                            crouch += 1;
                            if crouch < 3 {
                                pad.buttons = buttons::CROSS;
                            }
                        }
                        if crouch >= 3 && run.wall_ride == 0 {
                            pad.buttons |= buttons::TRIANGLE;
                        }
                        // Jumping out: an ollie 15 frames into the ride.
                        if jump_out && ollie_at.is_none() && run.wall_ride == 15 {
                            ollie_at = Some(frame);
                        }
                        if ollie_at.is_some_and(|f| frame - f < 3) {
                            pad.buttons |= buttons::CROSS;
                        }
                        last = local(engine.step(&pad).unwrap().unwrap());
                        run.note(&last, 0.0, &mut streak);
                        most = most.max(past(&last, 0.0));
                    }
                    tries += 1;
                    if run.wall_ride > 0 {
                        total += 1;
                    }
                    if most > 0.0005 || run.longest > 3 {
                        problems.push(format!(
                            "wall turned {turn}° at {centre}, glancing {degrees}°, jump out {jump_out}: furthest past the wall {most:+.4} m, on it as floor {} frames",
                            run.longest
                        ));
                    }
                    rides.push(run.wall_ride);
                    furthest.push(format!("{most:+.4}"));
                }
                eprintln!(
                    "WALLRIDE wall turned {turn}° at {centre}, glancing {degrees}°, jump out {jump_out}: frames in a wall ride {rides:?}; furthest past the wall m {furthest:?}"
                );
            }
        }
    }
    eprintln!("wall rides started: {total}/{tries}");
    if total * 4 < tries * 3 {
        problems.push(format!("only {total}/{tries} wall rides started"));
    }
    problems
}
