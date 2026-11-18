//! THUG2's own HUD (the score and special meter, the trick string, its balance meter, goal messages) as the
//! contract's [`HudFrame`]: the game's 2D as the harness takes it down each frame (`thug2_sys::Engine::hud`), cut
//! into parts ([`Part`]), each with a picture and a place of its own. The game scatters its HUD over its 4:3 screen;
//! in the room the score panel and the trick string are by the player's chosen controller (the score just over it,
//! or above the meter when that's the left one too; the trick string, which grows long, to its left, its end by the
//! controller), and the balance meter is at the skater as in the game: a grind's or lip's arc over its head, a
//! manual's upright meter beside it.
//!
//! The game draws its 2D in Direct3D 9: each draw a triangle list of screen vertices with one texture, its colour
//! the texture's times the vertices' (doubled, for the game's 2D), worked in the game's own gamma values, blended
//! over what's under it (or added): [`Shading::Modulate`] with `gamma`.
use engine_core::glam::Vec2;
use engine_core::hud::{HudBlend, HudDraw, HudFrame, HudPart, HudVertex, Placement, Rgba8, Shading, Side, TextureId};
use thug2_sys as t2;

/// A part of the HUD, with a picture and a place of its own.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Part {
    /// The score and the special meter: the screen's top left.
    Score,
    /// The balance meter (grind, lip: an arc over the middle; manual: one upright, left of it).
    Meter,
    /// The trick string, its points and multiplier (the bottom), and whatever else the game shows (messages, the
    /// freak-out meter after a bail).
    Trick,
}

/// The balance meter's panel's width against the skater's height (as it is drawn), and the least share of its
/// distance from the eyes it may be (about 9 degrees across: the meter itself some 6, legible from across a room
/// and with the skater small).
pub const METER_PER_SKATER: f32 = 1.2;
pub const METER_LEAST_SHARE: f32 = 0.16;
/// The skater's middle, a share of its height up from its feet (an upright meter is beside it).
pub const METER_MIDDLE: f32 = 0.55;
/// The skater's height, the stage's metres.
pub const SKATER_HEIGHT: f32 = 1.8;

impl Part {
    pub const ALL: [Part; 3] = [Part::Score, Part::Meter, Part::Trick];

    /// Its name (the pictures' and the logs').
    pub fn name(self) -> &'static str {
        match self {
            Part::Score => "score",
            Part::Meter => "meter",
            Part::Trick => "trick",
        }
    }

    /// The part of the screen its picture shows, pixels.
    pub fn size(self) -> Vec2 {
        match self {
            Part::Score => Vec2::new(220.0, 110.0),
            Part::Meter => Vec2::new(200.0, 160.0),
            Part::Trick => Vec2::new(640.0, 260.0),
        }
    }

    /// Where it goes: the score and the trick string by a controller (real metres a screen pixel, and where from the
    /// grip: offset and anchor; the score, sharing the left controller with the meter, with its bottom middle above
    /// it), the meter at the skater (an upright one, a manual's, beside it; an arc over its head).
    pub fn placement(self, upright: bool) -> Placement {
        match self {
            // (its bottom middle just over the grip)
            Part::Score => Placement::Hand {
                metres_per_px: 0.00055,
                offset: Vec2::new(0.0, 0.07),
                anchor: Vec2::new(0.0, -0.5),
                hand: None,
                on_left: Some((Vec2::new(0.0, 0.135), Vec2::new(0.0, -0.5))),
            },
            Part::Meter => Placement::AtPlayer {
                side: if upright { Side::Beside } else { Side::Above },
                width_per_height: METER_PER_SKATER,
                least_view_share: METER_LEAST_SHARE,
                middle: METER_MIDDLE,
                // (an upright meter is about a fifth of the panel across: just clear of the side; an arc is about a
                // third of the panel high, its middle the panel's: just over the head)
                clear: if upright { (0.3, 0.15) } else { (0.0, 0.2) },
            },
            // (its right edge, the string's end, left of the controller)
            Part::Trick => Placement::Hand {
                metres_per_px: 0.0006,
                offset: Vec2::new(-0.07, 0.03),
                anchor: Vec2::new(0.5, 0.0),
                hand: None,
                on_left: None,
            },
        }
    }

    /// What's drawn is an upright meter (taller than wide), not an arc.
    pub fn upright(drawn: Option<(Vec2, Vec2)>) -> bool {
        drawn.is_some_and(|(min, max)| max.y - min.y > max.x - min.x)
    }

    /// Which part a draw is, by where it is on the screen (`bounds`: its left top and right bottom, pixels), and
    /// whether the skater is balancing (only then is a small piece over the middle the balance meter's: otherwise it
    /// is the freak-out meter's, or a message's, and goes with the rest).
    pub fn of(bounds: (Vec2, Vec2), balancing: bool) -> Part {
        let (min, max) = bounds;
        let middle = (min + max) / 2.0;
        let size = max - min;
        if middle.y < 90.0 && middle.x < 330.0 {
            Part::Score
        } else if balancing && middle.y < 330.0 && (160.0..480.0).contains(&middle.x) && size.max_element() < 180.0 {
            Part::Meter
        } else {
            Part::Trick
        }
    }

    /// The screen point at the middle of its picture: the score where the game puts it; the meter centred on what's
    /// drawn; the trick string from its left, centred up and down.
    pub fn view(self, drawn: Option<(Vec2, Vec2)>) -> Vec2 {
        let size = self.size();
        match (self, drawn) {
            (Part::Score, _) | (_, None) => size / 2.0,
            (Part::Meter, Some((min, max))) => (min + max) / 2.0,
            (Part::Trick, Some((min, max))) => Vec2::new(max.x - size.x / 2.0, (min.y + max.y) / 2.0),
        }
    }
}

/// THUG2's 2D after a step (`hud`, none taken down: every part empty) as a [`HudFrame`], into `out` (its buffers
/// kept). `balancing`: the skater is balancing a grind, lip or manual (its meter is shown at it).
pub fn frame(hud: &t2::Hud, balancing: bool, out: &mut HudFrame) {
    out.clear();
    let [width, height] = t2::HUD_SIZE;
    out.canvas = Vec2::new(width, height);
    out.vertices.extend(hud.vertices.iter().map(|v| HudVertex {
        position: Vec2::from_array(v.position),
        uv: Vec2::from_array(v.uv),
        // (thug2-sys has the game's ARGB as RGBA already)
        color: v.color,
    }));
    // (what each part has drawn: its bounds on the screen)
    let mut drawn: [Option<(Vec2, Vec2)>; 3] = [None; 3];
    for draw in &hud.draws {
        let vertices = hud.vertices.get(draw.first_vertex as usize..(draw.first_vertex + draw.vertex_count) as usize).unwrap_or(&[]);
        let bounds = vertices.iter().fold((Vec2::splat(f32::MAX), Vec2::splat(f32::MIN)), |(min, max), v| {
            let p = Vec2::from_array(v.position);
            (min.min(p), max.max(p))
        });
        let part = Part::of(bounds, balancing);
        let k = Part::ALL.iter().position(|p| *p == part).unwrap_or(0);
        if !vertices.is_empty() {
            drawn[k] = Some(drawn[k].map_or(bounds, |(min, max)| (min.min(bounds.0), max.max(bounds.1))));
        }
        out.draws.push(HudDraw {
            first: draw.first_vertex,
            count: draw.vertex_count,
            texture: draw.texture.map(texture_id),
            part: k as u16,
            blend: match draw.blend {
                t2::HudBlend::Alpha => HudBlend::Alpha,
                t2::HudBlend::Add => HudBlend::Add,
                t2::HudBlend::Opaque => HudBlend::Opaque,
            },
            shading: Shading::Modulate { scale: if draw.doubled { 2.0 } else { 1.0 }, gamma: true },
        });
    }
    out.parts.extend(Part::ALL.iter().enumerate().map(|(k, part)| HudPart {
        name: part.name().into(),
        size: part.size(),
        view: part.view(drawn[k]),
        resolution: 1.0,
        empty: drawn[k].is_none(),
        placement: part.placement(Part::upright(drawn[k])),
    }));
}

/// A game texture's id as the contract's (the game's are never negative).
pub fn texture_id(texture: i32) -> TextureId {
    texture as TextureId
}

/// The game's texture for a contract id.
pub fn game_texture(id: TextureId) -> i32 {
    id as i32
}

/// A texture as the contract's.
pub fn texture(texture: t2::Texture) -> Rgba8 {
    Rgba8 { width: texture.width.max(1), height: texture.height.max(1), pixels: texture.rgba }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The game's 2D parts, where it draws them (thug2-drive's captures): the score panel's pieces, the grind and
    /// manual meters' arcs and markers, the trick string.
    #[test]
    fn draws_go_to_their_parts_by_where_the_game_puts_them() {
        let at = |x0: f32, y0: f32, x1: f32, y1: f32| Part::of((Vec2::new(x0, y0), Vec2::new(x1, y1)), true);
        assert_eq!(at(9.0, 27.0, 195.0, 90.0), Part::Score);
        assert_eq!(at(136.0, 34.0, 195.0, 60.0), Part::Score);
        // (the grind arc and its marker)
        assert_eq!(at(256.0, 145.0, 384.0, 197.0), Part::Meter);
        assert_eq!(at(313.0, 144.0, 329.0, 164.0), Part::Meter);
        // (the manual's upright arc and marker, left of the middle)
        assert_eq!(at(233.0, 175.0, 266.0, 303.0), Part::Meter);
        assert_eq!(at(215.0, 233.0, 240.0, 250.0), Part::Meter);
        // (the trick string and its points)
        assert_eq!(at(250.0, 392.0, 376.0, 408.0), Part::Trick);
        assert_eq!(at(282.0, 372.0, 358.0, 388.0), Part::Trick);
        // (a message across the middle of the screen)
        assert_eq!(at(60.0, 200.0, 580.0, 260.0), Part::Trick);
        // (after a bail, not balancing: the freak-out meter's fist, ticks and bar stay together with the rest)
        let bailed = |x0: f32, y0: f32, x1: f32, y1: f32| Part::of((Vec2::new(x0, y0), Vec2::new(x1, y1)), false);
        assert_eq!(bailed(205.0, 100.0, 245.0, 160.0), Part::Trick);
        assert_eq!(bailed(324.0, 136.0, 330.0, 156.0), Part::Trick);
        assert_eq!(bailed(224.0, 136.0, 432.0, 158.0), Part::Trick);
        assert_eq!(bailed(9.0, 27.0, 195.0, 90.0), Part::Score);
    }

    /// The trick string is seen from its end; the meter from its middle; the score where the game puts it.
    #[test]
    fn each_part_is_looked_at_where_it_is() {
        let string = (Vec2::new(250.0, 370.0), Vec2::new(376.0, 410.0));
        assert_eq!(Part::Trick.view(Some(string)), Vec2::new(376.0 - 320.0, 390.0));
        let arc = (Vec2::new(256.0, 145.0), Vec2::new(384.0, 197.0));
        assert_eq!(Part::Meter.view(Some(arc)), Vec2::new(320.0, 171.0));
        assert!(!Part::upright(Some(arc)));
        // (a manual's: its left side at the panel's left, by the controller)
        let manual = (Vec2::new(215.0, 175.0), Vec2::new(266.0, 303.0));
        assert!(Part::upright(Some(manual)));
        assert_eq!(Part::Meter.view(Some(manual)), Vec2::new(240.5, 239.0));
        assert_eq!(Part::Score.view(Some(arc)), Vec2::new(110.0, 55.0));
    }

    fn quad(hud: &mut t2::Hud, min: [f32; 2], max: [f32; 2], texture: Option<i32>, blend: t2::HudBlend, doubled: bool) {
        let first = hud.vertices.len() as u32;
        let corners = [[min[0], min[1]], [max[0], min[1]], [max[0], max[1]], [min[0], max[1]]];
        for i in [0, 1, 2, 0, 2, 3] {
            hud.vertices.push(t2::HudVertex { position: corners[i], uv: [0.0, 0.0], color: [10, 20, 30, 40] });
        }
        hud.draws.push(t2::HudDraw { first_vertex: first, vertex_count: 6, texture, blend, doubled });
    }

    /// A frame mid-grind: each draw in its part, as the game draws it (doubled, in gamma values; its blend), each
    /// part looked at where it is and placed as it says; the colours as the game gave them.
    #[test]
    fn a_frame_mid_grind_is_cut_into_its_parts() {
        let mut hud = t2::Hud::default();
        quad(&mut hud, [9.0, 27.0], [195.0, 90.0], Some(3), t2::HudBlend::Alpha, true);
        quad(&mut hud, [256.0, 145.0], [384.0, 197.0], Some(5), t2::HudBlend::Add, true);
        quad(&mut hud, [250.0, 392.0], [376.0, 408.0], None, t2::HudBlend::Opaque, false);
        let mut frame = HudFrame::default();
        super::frame(&hud, true, &mut frame);
        assert_eq!(frame.canvas, Vec2::new(640.0, 480.0));
        assert_eq!(frame.vertices.len(), 18);
        assert_eq!(frame.vertices[0].color, [10, 20, 30, 40]);
        assert_eq!(frame.draws.iter().map(|d| d.part).collect::<Vec<_>>(), [0, 1, 2]);
        assert_eq!(frame.draws[0].texture, Some(3));
        assert_eq!(frame.draws[1].blend, HudBlend::Add);
        assert_eq!(frame.draws[1].shading, Shading::Modulate { scale: 2.0, gamma: true });
        assert_eq!(frame.draws[2].shading, Shading::Modulate { scale: 1.0, gamma: true });
        assert_eq!((frame.draws[2].texture, frame.draws[2].blend), (None, HudBlend::Opaque));
        assert_eq!(frame.parts.iter().map(|p| p.name.as_str()).collect::<Vec<_>>(), ["score", "meter", "trick"]);
        assert!(frame.parts.iter().all(|p| !p.empty));
        assert_eq!(frame.parts[1].view, Vec2::new(320.0, 171.0));
        assert!(matches!(frame.parts[1].placement, Placement::AtPlayer { side: Side::Above, .. }));
        assert_eq!(frame.parts[2].view, Vec2::new(376.0 - 320.0, 400.0));
        // (not balancing: the piece over the middle is the trick part's, and the meter has nothing)
        super::frame(&hud, false, &mut frame);
        assert_eq!(frame.draws[1].part, 2);
        assert!(frame.parts[1].empty);
        // (nothing taken down: every part empty)
        super::frame(&t2::Hud::default(), false, &mut frame);
        assert!(frame.draws.is_empty() && frame.parts.len() == 3 && frame.parts.iter().all(|p| p.empty));
    }

    /// A manual's meter (taller than wide) is beside the skater.
    #[test]
    fn a_manual_meter_is_beside() {
        let mut hud = t2::Hud::default();
        quad(&mut hud, [215.0, 175.0], [266.0, 303.0], Some(1), t2::HudBlend::Alpha, true);
        let mut frame = HudFrame::default();
        super::frame(&hud, true, &mut frame);
        assert!(matches!(frame.parts[1].placement, Placement::AtPlayer { side: Side::Beside, .. }));
    }
}
