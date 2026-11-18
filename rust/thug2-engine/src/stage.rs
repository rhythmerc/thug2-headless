//! The stage and park as THUG2's world, putting the skater down, and where it is.
//!
//! - **Units:** the game's are inches, y up, right-handed like the contract's space, so the stage's metres map onto
//!   it by scale alone ([`t2::INCHES_PER_METRE`]), offset ([`OFFSET`]) clear of the empty level's own scene. How big
//!   the room is to the game is the runtime's (the player's world scale, the skater size), as for every engine.
//!   [`to_game`], [`from_game`].
//! - **Faces:** the game rides a face only from its front, so each is turned the way it is met ([`triangles`]).
use engine_core::glam::Vec3;
use engine_core::skate::{ParkPiece, SkatePark, SkaterLink};
use engine_core::{Applied, Frame, Solid, Stage};
use thug2_sys as t2;

/// Where the contract's origin is in the game's world (inches): clear of the empty level's sky.
pub const OFFSET: Vec3 = Vec3::new(50_000.0, 0.0, 50_000.0);
/// A face whose normal is less up than this is upright (a wall: steeper than 75°); one more up than `RAMP_UP` is level
/// ground (under 18°), between them a ramp. A piece's transition steeper than `VERT_UP` (45°), joined to the ramp below
/// it, is vert: the game's vert air, spine transfers and hips need it (they look for vert faces steeper than 45°).
pub const WALL_UP: f32 = 0.26;
pub const RAMP_UP: f32 = 0.95;
pub const VERT_UP: f32 = 0.707;
/// Where the deck is above the skater's feet, metres.
pub const DECK_HEIGHT: f32 = 0.08;
/// Where the skater's sounds come from: its board, a little up from its feet, metres.
pub const SOUND_HEIGHT: f32 = 0.3;
/// How far over the ground point the skater is put down, metres.
pub const PLACE_LIFT: f32 = 0.1;

/// Contract metres as the game's units.
pub fn to_game(p: Vec3) -> [f32; 3] {
    (p * t2::INCHES_PER_METRE + OFFSET).to_array()
}

pub fn from_game(p: [f32; 3]) -> Vec3 {
    (Vec3::from_array(p) - OFFSET) / t2::INCHES_PER_METRE
}

/// Where to put the skater down at `point`, facing `forward`.
pub fn spawn(point: Vec3, forward: Vec3) -> t2::Spawn {
    t2::Spawn { point: to_game(point + Vec3::Y * PLACE_LIFT), heading_degrees: t2::heading_of(forward.to_array()) }
}

/// Where the skater is: the rider at its feet, the board [`DECK_HEIGHT`] up from them, life size. (The game's C API
/// gives one frame, the skater's: no board of its own, and no walking off it.)
pub fn link(s: &t2::Skater) -> SkaterLink {
    let feet = from_game(s.position);
    let up = Vec3::from_array(s.up);
    let forward = Vec3::from_array(s.forward);
    SkaterLink {
        rider: Some(Frame { position: feet, forward, up: up.normalize_or(Vec3::Y) }),
        board: Some(Frame { position: feet + up * DECK_HEIGHT, forward, up }),
        on_board: true,
        grinding: s.state == t2::State::Rail,
        size: 1.0,
    }
}

/// The game's rails for `park`.
pub fn rails(park: &SkatePark) -> Vec<Vec<[f32; 3]>> {
    park.rails.iter().map(|r| r.iter().map(|&p| to_game(p)).collect()).collect()
}

/// The room and pieces as the game's collision faces (game units, offset). The game rides a face only from its
/// front, so each is turned the way it is met:
/// - ground (level to a steep ramp): facing up, skatable;
/// - a piece's transition steeper than 45° above a ramp (a quarter pipe's top half): vert, facing the ramp, so the
///   skater goes straight up and comes back down into it - or transfers to a vert face behind the coping (a spine);
/// - any other upright face (room walls, furniture and box sides): both ways, wallridable and not skatable - the wall
///   flag alone, as a real level's wallridable walls carry it (the game takes a face flagged skatable as ground
///   whatever its other flags: it rode such walls as floor, off a quarter pipe against one, and never wallrode them).
///   (The two sides are in one plane: thug2-decomp passes over the back one where the game would otherwise take it
///   from the front - its wall plant placing the skater off the wall, and its keep-above-ground feeler under a wall
///   overhanging the room, which lifted the skater onto the wall's plane and so through it.)
pub fn triangles(stage: &Stage, park: &SkatePark) -> Vec<t2::Triangle> {
    let mut out = Vec::new();
    for t in stage.room() {
        face(&mut out, t, None);
    }
    for solid in &stage.solids {
        piece_faces(&mut out, solid, park.piece(solid.id));
    }
    out
}

fn normal(t: [Vec3; 3]) -> Vec3 {
    (t[1] - t[0]).cross(t[2] - t[0]).normalize_or_zero()
}

/// One face; `vert`: it is a quarter pipe's top, facing (across) `vert`.
fn face(out: &mut Vec<t2::Triangle>, t: [Vec3; 3], vert: Option<Vec3>) {
    use t2::face::*;
    let n = normal(t);
    if n == Vec3::ZERO {
        return;
    }
    let flipped = [t[0], t[2], t[1]];
    let game = |t: [Vec3; 3], flags: u16| t2::Triangle { v: t.map(to_game), flags, terrain: 0 };
    if let Some(toward) = vert {
        let t = if n.with_y(0.0).dot(toward.with_y(0.0)) >= 0.0 { t } else { flipped };
        out.push(game(t, SKATABLE | VERT));
    } else if n.y.abs() >= WALL_UP {
        // (ground, or a slope or overhang turned to face up)
        out.push(game(if n.y > 0.0 { t } else { flipped }, SKATABLE));
    } else {
        // (WALL alone, as a real level's wallridable walls are: the game reads SKATABLE first, and a skatable face
        // is ground to it - it rode such walls as floor and never wallrode them)
        out.push(game(t, WALL));
        out.push(game(flipped, WALL));
    }
}

/// A piece's faces (its body's, then its rail tubes'), its quarter pipes' tops found: a quad steeper than 45° whose
/// lower edge is the upper edge of a gentler ramp quad (or of another such top) is vert, facing that ramp.
fn piece_faces(out: &mut Vec<t2::Triangle>, solid: &Solid, piece: Option<&ParkPiece>) {
    let tubes = piece.map_or(0..0, |p| p.rail_quads.clone());
    let quads: Vec<[Vec3; 4]> = (solid.quads.iter().enumerate().filter(|(i, _)| !tubes.contains(i)))
        .chain(solid.quads.iter().enumerate().filter(|(i, _)| tubes.contains(i)))
        .map(|(_, q)| *q)
        .collect();
    let up_normal = |q: &[Vec3; 4]| {
        let n = normal([q[0], q[1], q[2]]);
        if n.y < 0.0 { -n } else { n }
    };
    // (a quad's two lowest corners)
    let bottom = |q: &[Vec3; 4]| {
        let mut c = *q;
        c.sort_by(|a, b| a.y.total_cmp(&b.y));
        [c[0], c[1]]
    };
    let has = |q: &[Vec3; 4], p: Vec3| q.iter().any(|c| c.distance_squared(p) < 1e-4);
    // Each quad's vert facing: none, or the ramp's way.
    let mut vert: Vec<Option<Vec3>> = vec![None; quads.len()];
    for _ in 0..quads.len().min(32) {
        let mut changed = false;
        for (i, q) in quads.iter().enumerate() {
            if vert[i].is_some() || up_normal(q).y >= VERT_UP {
                continue;
            }
            let [a, b] = bottom(q);
            let below = quads.iter().enumerate().find_map(|(j, r)| {
                let n = up_normal(r);
                let ramp = n.y >= VERT_UP && n.y < RAMP_UP;
                let joins = j != i && has(r, a) && has(r, b);
                if !joins {
                    None
                } else if ramp {
                    Some(n)
                } else {
                    vert[j]
                }
            });
            if let Some(toward) = below.filter(|n| n.with_y(0.0).length_squared() > 1e-6) {
                vert[i] = Some(toward);
                changed = true;
            }
        }
        if !changed {
            break;
        }
    }
    for (q, v) in quads.iter().zip(vert) {
        let [a, b, c, d] = *q;
        face(out, [a, b, c], v);
        face(out, [a, c, d], v);
    }
}

/// What the game has of the stage: the park kept until its stage comes, and the revision it was given.
#[derive(Default)]
pub struct StageFeed {
    park: Option<SkatePark>,
    given: Option<u64>,
    /// What it was last given: faces and rails.
    pub faces: usize,
    pub rails: usize,
}

impl StageFeed {
    /// The stage revision the game has (none: it has none of ours yet: the skater is still at the level's start).
    pub fn given(&self) -> Option<u64> {
        self.given
    }

    /// The park, kept for the stage of its revision ([`StageFeed::set_stage`] takes the two together).
    pub fn set_park(&mut self, park: &SkatePark) -> Applied {
        if self.given == Some(park.revision) {
            return Applied::Done;
        }
        if self.park.as_ref().is_none_or(|p| p.revision != park.revision) {
            self.park = Some(park.clone());
        }
        Applied::Deferred
    }

    /// The stage (with its park) given to the game when it changes: not mid-grind (the rails would go from under it),
    /// and only once there's somewhere to put the skater (`spawn`, taken with it) or the game has a stage of ours
    /// already. `engine`: none while the game isn't running yet (a waiting `spawn` is kept).
    pub fn set_stage(
        &mut self,
        engine: Option<&mut t2::Engine>,
        current: Option<&t2::Skater>,
        stage: &Stage,
        spawn: &mut Option<t2::Spawn>,
    ) -> Applied {
        let grinding = current.is_some_and(|s| s.state == t2::State::Rail);
        if stage.revision == 0 || self.given == Some(stage.revision) {
            return Applied::Done;
        }
        if grinding {
            return Applied::Deferred;
        }
        let Some(park) = self.park.as_ref().filter(|p| p.revision == stage.revision) else { return Applied::Deferred };
        if spawn.is_none() && self.given.is_none() {
            return Applied::Deferred;
        }
        let Some(engine) = engine else { return Applied::Deferred };
        let faces = triangles(stage, park);
        let rails = rails(park);
        engine.set_world(&faces, spawn.take());
        engine.set_rails(&rails);
        (self.faces, self.rails) = (faces.len(), rails.len());
        self.given = Some(stage.revision);
        Applied::Done
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn up(t: &t2::Triangle) -> f32 {
        let v = t.v.map(Vec3::from_array);
        normal([v[0], v[1], v[2]]).y
    }

    fn piece(quads: Vec<[Vec3; 4]>) -> (Solid, ParkPiece) {
        (Solid { id: 1, quads }, ParkPiece { solid: 1, ..Default::default() })
    }

    #[test]
    fn metres_map_to_offset_inches_and_back() {
        let p = Vec3::new(1.0, 2.0, -3.0);
        let g = to_game(p);
        assert!((g[1] - 2.0 * 39.370_08).abs() < 1e-3);
        assert!((from_game(g) - p).length() < 1e-4);
    }

    #[test]
    fn ground_faces_up_whichever_way_it_was_wound_and_walls_both_ways() {
        let mut out = Vec::new();
        // A floor wound to face down, and a wall.
        face(&mut out, [Vec3::ZERO, Vec3::X, Vec3::Z], None);
        assert_eq!(out.len(), 1);
        assert!(up(&out[0]) > 0.99, "turned up");
        assert_eq!(out[0].flags, t2::face::SKATABLE);
        face(&mut out, [Vec3::ZERO, Vec3::Y, Vec3::X], None);
        assert_eq!(out.len(), 3, "a wall is two faces");
        assert_eq!(out[1].flags, t2::face::WALL, "wallridable, not ground");
        assert_eq!(out[2].flags, t2::face::WALL);
    }

    #[test]
    fn a_quarter_pipes_top_is_vert_facing_its_ramp() {
        // A quarter pipe along +z: flat, a 45° ramp, then an upright top (wound facing away, +z).
        let r = 1.0;
        let flat = [Vec3::new(0.0, 0.0, -1.0), Vec3::new(1.0, 0.0, -1.0), Vec3::new(1.0, 0.0, 0.0), Vec3::new(0.0, 0.0, 0.0)];
        let ramp = [Vec3::new(0.0, 0.0, 0.0), Vec3::new(1.0, 0.0, 0.0), Vec3::new(1.0, 0.5 * r, 0.5 * r), Vec3::new(0.0, 0.5 * r, 0.5 * r)];
        let top = [Vec3::new(0.0, 0.5 * r, 0.5 * r), Vec3::new(1.0, 0.5 * r, 0.5 * r), Vec3::new(1.0, 1.5 * r, 0.5 * r), Vec3::new(0.0, 1.5 * r, 0.5 * r)];
        // A box side elsewhere, on nothing: a wall.
        let side = [Vec3::new(3.0, 0.0, 0.0), Vec3::new(3.0, 0.5, 0.0), Vec3::new(3.0, 0.5, 1.0), Vec3::new(3.0, 0.0, 1.0)];
        let (solid, park) = piece(vec![flat, ramp, top, side]);
        let mut out = Vec::new();
        piece_faces(&mut out, &solid, Some(&park));
        let verts: Vec<&t2::Triangle> = out.iter().filter(|t| t.flags & t2::face::VERT != 0).collect();
        assert_eq!(verts.len(), 2, "the top's two triangles");
        for t in verts {
            let v = t.v.map(Vec3::from_array);
            assert!(normal([v[0], v[1], v[2]]).z < -0.9, "facing back down the ramp (-z)");
        }
        let walls = out.iter().filter(|t| t.flags & t2::face::WALL != 0).count();
        assert_eq!(walls, 4, "the box side, both ways");
        assert!(out.iter().filter(|t| t.flags == t2::face::SKATABLE).all(|t| up(t) > 0.3));
    }

    #[test]
    fn a_transition_is_vert_from_45_degrees_up() {
        // Up +z: a 30° ramp, then 60°, then 85° (each quad's lower edge the last one's upper edge).
        let mut quads = Vec::new();
        let mut at = Vec3::ZERO;
        for degrees in [30.0f32, 60.0, 85.0] {
            let rise = Vec3::new(0.0, degrees.to_radians().sin(), degrees.to_radians().cos()) * 0.3;
            let next = at + rise;
            quads.push([at, at + Vec3::X, next + Vec3::X, next]);
            at = next;
        }
        let (solid, park) = piece(quads);
        let mut out = Vec::new();
        piece_faces(&mut out, &solid, Some(&park));
        let vert: Vec<bool> = out.iter().map(|t| t.flags & t2::face::VERT != 0).collect();
        assert_eq!(vert, vec![false, false, true, true, true, true], "the 30° quad isn't vert, the 60° and 85° are");
        for t in out.iter().filter(|t| t.flags & t2::face::VERT != 0) {
            let v = t.v.map(Vec3::from_array);
            assert!(normal([v[0], v[1], v[2]]).z < 0.0, "facing back down the ramp");
        }
    }

    fn skater(state: t2::State) -> t2::Skater {
        t2::Skater { position: to_game(Vec3::ZERO), velocity: [0.0; 3], forward: [0.0, 0.0, 1.0], up: [0.0, 1.0, 0.0], state, frame: 0, balance: None }
    }

    /// The stage is taken later, not now: mid-grind, before its park, and before there's anywhere to put the skater;
    /// and nothing is owed for revision 0. (The game itself isn't needed for these: it's never reached.)
    #[test]
    fn a_stage_waits_for_the_grind_its_park_and_a_place() {
        let stage = Stage { revision: 2, ..Default::default() };
        let park = SkatePark { revision: 2, ..Default::default() };
        let mut feed = StageFeed::default();
        let mut spawn = Some(spawn(Vec3::ZERO, Vec3::Z));
        assert_eq!(feed.set_stage(None, None, &Stage::default(), &mut spawn), Applied::Done, "nothing built yet");
        assert_eq!(feed.set_stage(None, None, &stage, &mut spawn), Applied::Deferred, "no park yet");
        assert_eq!(feed.set_park(&park), Applied::Deferred);
        let grinding = skater(t2::State::Rail);
        assert_eq!(feed.set_stage(None, Some(&grinding), &stage, &mut spawn), Applied::Deferred, "mid-grind");
        assert!(spawn.is_some(), "the place kept");
        let rolling = skater(t2::State::Ground);
        assert_eq!(feed.set_stage(None, Some(&rolling), &stage, &mut None), Applied::Deferred, "nowhere to put it");
        assert_eq!(feed.set_stage(None, Some(&rolling), &stage, &mut spawn), Applied::Deferred, "the game not running");
        assert!(spawn.is_some(), "the place kept for it");
        assert_eq!(feed.given(), None);
    }

    #[test]
    fn the_skater_is_its_feet_and_its_board_a_deck_up() {
        let s = t2::Skater {
            position: to_game(Vec3::new(1.0, 0.0, 2.0)),
            velocity: [0.0; 3],
            forward: [0.0, 0.0, 1.0],
            up: [0.0, 1.0, 0.0],
            state: t2::State::Rail,
            frame: 0,
            balance: None,
        };
        let link = link(&s);
        let (rider, board) = (link.rider.unwrap(), link.board.unwrap());
        assert!((rider.position - Vec3::new(1.0, 0.0, 2.0)).length() < 1e-4);
        assert!((board.position.y - DECK_HEIGHT).abs() < 1e-4, "a deck up");
        assert!(link.grinding && link.on_board);
        assert_eq!(link.size, 1.0, "life size");
        let p = spawn(Vec3::ZERO, Vec3::Z);
        assert!((p.point[1] - PLACE_LIFT * t2::INCHES_PER_METRE).abs() < 1e-3);
    }
}
