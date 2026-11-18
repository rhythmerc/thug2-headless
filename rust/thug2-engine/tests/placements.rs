//! The game's own count of the skater put somewhere outside its motion (`t2::Engine::skater_placements`), on the real
//! engine (`THUG2_GAME_DIR`: the folder holding the player's `Data`; skipped without it). One test (the engine boots
//! once a process):
//! - a wall plant: the step whose pose jumps (the skater turned round and set off the wall at once) changes the count;
//!   and a late plant: the step the game backs the skater off the wall to wait for the press, and the plant;
//! - riding, ollies, kickflips, air spins, bails and their landings change it only on a step whose pose jumped (the
//!   game turning the skater round as it lands backwards); the jumps it doesn't count, and every step it doesn't
//!   count that turned more than 60 degrees, are listed (the model layer's own limits snap the jumps).
use engine_core::Stage;
use engine_core::glam::{Mat4, Quat, Vec3};
use engine_core::skate::SkatePark;
use std::path::Path;
use thug2_engine::stage;
use thug2_sys::{self as t2, PAD_AT_REST, Pad, State, buttons};

/// How far the wall is from the room's middle, metres: the room is on its near side.
const WALL_AT: f32 = 3.0;
const PUSH: Pad = Pad { left_y: 0, ..PAD_AT_REST };
/// A turn in one step this big (degrees), of the root or of a joint, is a jump, not a motion: the model layer's
/// limits at 60 Hz (skate-game's `model::JUMP_TURN_RATE` and `JOINT_JUMP_TURN_RATE`).
const ROOT_JUMP: f32 = 90.0;
const JOINT_JUMP: f32 = 150.0;

/// One step as drawn: the state, whether the count changed, the root's turn and move from the step before (degrees,
/// metres), whether the root is mirrored, and the palette's largest turn of a joint from the step before (degrees).
#[derive(Debug, Clone, Copy)]
struct Step {
    frame: u32,
    state: State,
    placed: bool,
    root_turn: f32,
    root_move: f32,
    mirrored: bool,
    joint: usize,
    joint_turn: f32,
}

fn rotation(m: Mat4) -> Quat {
    m.to_scale_rotation_translation().1
}

fn turn(a: Mat4, b: Mat4) -> f32 {
    rotation(a).angle_between(rotation(b)).to_degrees()
}

/// Steps the engine with `pad` and measures the step against the pose before (`last`).
fn step(engine: &mut t2::Engine, pad: &Pad, last: &mut (engine_core::Pose, u64)) -> (t2::Skater, Step) {
    let skater = engine.step(pad).unwrap().unwrap();
    let placements = engine.skater_placements();
    let mut game = t2::Pose::default();
    assert!(engine.skater_pose(&mut game));
    let mut pose = engine_core::Pose::default();
    thug2_engine::model::pose(&game, &mut pose);
    let (before, count) = &*last;
    let (joint, joint_turn) =
        pose.joints.iter().zip(&before.joints).map(|(a, b)| turn(*a, *b)).enumerate().fold((0, 0.0), |m, (i, t)| if t > m.1 { (i, t) } else { m });
    let s = Step {
        frame: skater.frame,
        state: skater.state,
        placed: placements != *count,
        root_turn: turn(before.root, pose.root),
        root_move: (pose.root.w_axis - before.root.w_axis).truncate().length(),
        mirrored: pose.root.determinant() < 0.0,
        joint,
        joint_turn,
    };
    *last = (pose, placements);
    (skater, s)
}

/// A wall across +z at [`WALL_AT`], 60 m long and 3 m tall, both windings, on a floor.
fn room() -> Stage {
    let floor = [Vec3::new(-40.0, 0.0, -40.0), Vec3::new(-40.0, 0.0, 40.0), Vec3::new(40.0, 0.0, 40.0), Vec3::new(40.0, 0.0, -40.0)];
    let base = Vec3::Z * WALL_AT;
    let (along, up) = (Vec3::X * 30.0, Vec3::Y * 3.0);
    let wall = [base + along, base - along, base - along + up, base + along + up];
    let back = [wall[0], wall[3], wall[2], wall[1]];
    Stage { revision: 1, room_quads: vec![floor, wall, back], ..Default::default() }
}

#[test]
fn the_game_counts_its_wall_plants_and_turn_rounds() {
    let Some(dir) = std::env::var_os("THUG2_GAME_DIR") else {
        eprintln!("THUG2_GAME_DIR not set: skipped");
        return;
    };
    let cache = std::env::temp_dir().join("thug2-engine-test-cache");
    let mut engine = t2::boot(Path::new(&dir), Some(&cache), &["-level", "Default"]).expect("boot");
    for _ in 0..120 {
        engine.step(&PAD_AT_REST).expect("step");
    }
    let faces = stage::triangles(&room(), &SkatePark::default());
    let mut problems = Vec::new();

    // (every step not counted that turned more than 60 degrees, root or joint: what the model layer's limits must
    // let through or catch)
    let mut fast = Vec::new();
    // Wall plants: ollie at the wall from a little to the side each time, the plant tapped in the air; from the
    // ninth, late: nothing tapped until the wall is met (the game backs the skater off it to wait for the press).
    let (mut plants, mut late_plants) = (0, 0);
    for trial in 0..12 {
        let late = trial >= 8;
        let start = Vec3::new(-1.0 + 0.25 * trial as f32, 0.0, WALL_AT - 4.0);
        let before = engine.skater_placements();
        engine.set_world(&faces, Some(stage::spawn(start, Vec3::Z)));
        let mut last = (engine_core::Pose::default(), before);
        let (mut skater, _) = step(&mut engine, &PAD_AT_REST, &mut last);
        let (mut crouch, mut jumped) = (0, false);
        let mut steps = Vec::new();
        let (mut planted_at, mut backed_off) = (None, None);
        for frame in 0..400 {
            let mut pad = PUSH;
            let to_wall = WALL_AT - stage::from_game(skater.position).z;
            if !jumped && skater.state == State::Ground && to_wall < 1.6 + 0.1 * trial as f32 {
                crouch += 1;
                if crouch < 3 {
                    pad.buttons = buttons::CROSS;
                } else {
                    jumped = true;
                }
            } else if jumped && planted_at.is_none() && frame % 2 == 0 && (!late || backed_off.is_some()) {
                pad.buttons = buttons::CROSS;
            }
            let s;
            (skater, s) = step(&mut engine, &pad, &mut last);
            steps.push(s);
            if late && jumped && backed_off.is_none() && s.placed && s.state == State::Air {
                backed_off = Some(steps.len() - 1);
            }
            if s.state == State::Wallplant && planted_at.is_none() {
                planted_at = Some(steps.len() - 1);
            }
            if planted_at.is_some_and(|at| steps.len() > at + 30) {
                break;
            }
        }
        fast.extend(steps.iter().skip(1).filter(|s| !s.placed && (s.root_turn > 60.0 || s.joint_turn > 60.0)).copied());
        let Some(at) = planted_at else { continue };
        plants += 1;
        if late {
            // (the ollie's own press can plant at once: then there was no back-off)
            if backed_off.is_some_and(|back| back < at) {
                late_plants += 1;
            }
        }
        // (from a while before the plant: the wall met, perhaps backed off, then planted)
        let window = &steps[at.saturating_sub(12)..(at + 12).min(steps.len())];
        for s in window.iter().filter(|s| s.placed || s.root_turn > 20.0 || s.joint_turn > 45.0) {
            eprintln!(
                "plant {trial} frame {}: {:?}{} root turned {:.1}° moved {:.3} m{}, joint {} turned {:.1}°",
                s.frame,
                s.state,
                if s.placed { " PLACED" } else { "" },
                s.root_turn,
                s.root_move,
                if s.mirrored { " mirrored" } else { "" },
                s.joint,
                s.joint_turn
            );
        }
        // The step the skater was turned round on the wall is a placement.
        let jump = window.iter().max_by(|a, b| a.root_turn.total_cmp(&b.root_turn)).unwrap();
        if !jump.placed {
            problems.push(format!("plant {trial}: its largest turn ({:.1}° at frame {}) is not a placement", jump.root_turn, jump.frame));
        }
    }
    if plants < 6 {
        problems.push(format!("only {plants} plants of 12"));
    }
    if late_plants < 1 {
        problems.push("no late plant (a back-off counted, then the plant) of 4".into());
    }

    // Riding, an ollie, a kickflip and an air spin with their landings, in the room's middle (the wall far off).
    let start = Vec3::new(0.0, 0.0, WALL_AT - 20.0);
    engine.set_world(&faces, Some(stage::spawn(start, Vec3::Z)));
    let mut last = (engine_core::Pose::default(), engine.skater_placements());
    step(&mut engine, &PAD_AT_REST, &mut last);
    // (each step's largest turn, root or joint, in degrees; and the most of the steps that didn't jump)
    let mut smooth = (0.0f32, 0.0f32);
    let (mut marked, mut unmarked) = (Vec::new(), Vec::new());
    for frame in 0..600u32 {
        let mut pad = PUSH;
        let phase = frame % 150;
        // (pushing for a second, then the ollie: crouched, let go; a kickflip (square and left) or a spin (the stick
        // held left) in the air, by turns)
        if (60..64).contains(&phase) {
            pad.buttons = buttons::CROSS;
        }
        if (66..90).contains(&phase) {
            pad.left_x = 0;
            if (frame / 150) % 2 == 0 {
                pad.buttons = buttons::SQUARE;
            }
        }
        let (_, s) = step(&mut engine, &pad, &mut last);
        if frame == 0 {
            continue;
        }
        if !s.placed && (s.root_turn > 60.0 || s.joint_turn > 60.0) {
            fast.push(s);
        }
        let jumped = s.root_turn > ROOT_JUMP || s.joint_turn > JOINT_JUMP;
        if s.placed && !jumped {
            problems.push(format!("a placement on a step that didn't jump: {s:?}"));
        }
        if jumped {
            if s.placed { &mut marked } else { &mut unmarked }.push(s);
        } else {
            smooth = (smooth.0.max(s.root_turn), smooth.1.max(s.joint_turn));
        }
    }
    eprintln!(
        "riding, ollies, kickflips and spins: the most a smooth step turned: the root {:.1}°, a joint {:.1}°; jumps \
         the game counted: {marked:?}; jumps it didn't (the model layer's net snaps them): {unmarked:?}",
        smooth.0, smooth.1
    );
    for s in &fast {
        eprintln!("not counted, over 60 degrees: {s:?}");
    }
    assert!(problems.is_empty(), "{problems:#?}");
}
