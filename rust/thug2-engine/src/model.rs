//! The skater's model and pose as the contract's ([`engine_core::model`]): its meshes and plain materials, and each
//! step's palette and draw matrix carried into the contract's space.
//!
//! - **Meshes** ([`source`]): bind-pose vertices in game units, skinned by up to three of the palette's joints a
//!   vertex. The game's vertex colours are BGRA bytes with 0x80 as unchanged (the game doubles them): here they are
//!   multipliers, `/128`. A part's colour is its first pass's, also doubled. Some parts are wound the other way to
//!   the contract's (counter-clockwise front): turned. Alpha: blended, alpha-tested at its reference, or opaque.
//! - **Pose** ([`pose`]): the game's matrices are row-major for row vectors, read here as column ones (transposed),
//!   and the game clears their w column (its skinning reads only the rest, as for a point): w is 1 again here, or a
//!   product would drop the translations of the matrices before it. The root is (game to contract) x (draw matrix);
//!   the joints are the palette (a joint's inverse bind times its posed matrix), so skinning needs no inverse binds.
//!
//! - **Bodies** ([`board`]): the board is the parts lying flat and long under the skater in the bind pose; its
//!   joints are those only they are skinned to. It casts the skate mode's board footprint along its length.
//!
//! The C API gives no joint names, no parents and no joint positions (only the palette), so the model has no
//! skeleton shape: [`engine_core::model::Model::parents`] is empty, and the rider casts no shadow of its own (a
//! segment needs its joints' parents and positions).
use engine_core::glam::{Mat4, Vec3};
use engine_core::hud::Rgba8;
use engine_core::model::{AlphaMode, BodyGroup, Cast, Lighting, MaterialDesc, MeshData, Model, ModelSource, Point, Pose};
use engine_core::skate::body;
use thug2_sys as t2;

/// The joints the skater is posed with (the palette's most).
pub const JOINTS: u16 = 64;

/// The skater's model as the contract's.
pub fn source(model: &t2::SkaterModel) -> Model {
    Model {
        source: ModelSource::Meshes {
            meshes: model.meshes.iter().map(mesh).collect(),
            textures: model.textures.iter().map(|t| Rgba8 { width: t.width, height: t.height, pixels: t.rgba.clone() }).collect(),
            joint_count: JOINTS,
        },
        parents: Vec::new(),
        groups: board(&model.meshes).into_iter().collect(),
    }
}

/// A board part lies under this in the bind pose (game units: inches over the skater's feet)...
pub const BOARD_UNDER: f32 = 8.0;
/// ... and is at least this long, level (a deck is about 32 inches, a truck with its wheels 24 across its ends).
pub const BOARD_LONG: f32 = 20.0;
/// A joint is a part's when it weighs this much in one of its vertices.
const WEIGHED: f32 = 0.01;

/// The board's body: its parts (flat and long under the skater in the bind pose, [`BOARD_UNDER`], [`BOARD_LONG`]), its
/// joints (those only board parts are skinned to), cast along its length from its back end to its front (each end a
/// point of the bind pose carried by the board joint weighing most near it). None when no part looks like a board.
pub fn board(parts: &[t2::SkaterMesh]) -> Option<BodyGroup> {
    let joints_of = |part: &t2::SkaterMesh| {
        let mut mask = [false; 256];
        for v in &part.vertices {
            for k in 0..3 {
                if v.weights[k] > WEIGHED {
                    mask[usize::from(v.bones[k])] = true;
                }
            }
        }
        mask
    };
    let bounds = |part: &t2::SkaterMesh| {
        part.vertices.iter().fold((Vec3::MAX, Vec3::MIN), |(lo, hi), v| (lo.min(v.position.into()), hi.max(v.position.into())))
    };
    let is_board = |part: &t2::SkaterMesh| {
        let (lo, hi) = bounds(part);
        !part.indices.is_empty() && hi.y < BOARD_UNDER && (hi.x - lo.x).max(hi.z - lo.z) >= BOARD_LONG
    };
    let (mut board, mut rest) = ([false; 256], [false; 256]);
    for part in parts.iter().filter(|p| !p.indices.is_empty()) {
        let into = if is_board(part) { &mut board } else { &mut rest };
        for (j, used) in joints_of(part).into_iter().enumerate() {
            into[j] |= used;
        }
    }
    let joints: Vec<u16> = (0..256u16).filter(|&j| board[usize::from(j)] && !rest[usize::from(j)]).collect();
    if joints.is_empty() {
        return None;
    }
    // (its length: along x or z, whichever is longer; its ends a quarter of it from each end's middle)
    let vertices: Vec<&t2::ffi::Thug2Vertex> = parts.iter().filter(|p| is_board(p)).flat_map(|p| &p.vertices).collect();
    let (lo, hi) = vertices.iter().fold((Vec3::MAX, Vec3::MIN), |(lo, hi), v| (lo.min(v.position.into()), hi.max(v.position.into())));
    let axis = if hi.z - lo.z >= hi.x - lo.x { Vec3::Z } else { Vec3::X };
    let middle = (lo + hi) / 2.0;
    let half = (hi - lo).dot(axis) / 2.0;
    let end = |sign: f32| {
        let mut weight = [0.0f32; 256];
        for v in &vertices {
            if (Vec3::from(v.position) - middle).dot(axis) * sign > half * 0.5 {
                for k in 0..3 {
                    weight[usize::from(v.bones[k])] += v.weights[k];
                }
            }
        }
        let joint = joints.iter().copied().max_by(|a, b| weight[usize::from(*a)].total_cmp(&weight[usize::from(*b)]))?;
        Some(Point { joint, at: middle + axis * (half * sign) })
    };
    Some(BodyGroup {
        id: body::BOARD,
        cast: Cast::Footprint { back: end(-1.0)?, front: end(1.0)?, ellipses: body::BOARD_FOOTPRINT.to_vec() },
        joints,
    })
}

/// A part of the model (one for each of the game's, those with no triangles too: the runtime draws none of them).
fn mesh(part: &t2::SkaterMesh) -> MeshData {
    let v = &part.vertices;
    let mut indices = part.indices.clone();
    if wound_backwards(part) {
        for t in indices.chunks_exact_mut(3) {
            t.swap(1, 2);
        }
    }
    let colour = part.color.map(|c| (c * 2.0).min(1.0));
    MeshData {
        positions: v.iter().map(|v| v.position).collect(),
        normals: v.iter().map(|v| v.normal).collect(),
        uvs: v.iter().map(|v| v.uv).collect(),
        // (BGRA bytes, 0x80 unchanged: the game doubles them)
        colors: v
            .iter()
            .map(|v| {
                let [b, g, r, a] = v.color.to_le_bytes();
                [r, g, b, a].map(|c| (c as f32 / 128.0).min(1.0))
            })
            .collect(),
        joints: v.iter().map(|v| [v.bones[0], v.bones[1], v.bones[2], 0].map(u16::from)).collect(),
        weights: v
            .iter()
            .map(|v| {
                let sum: f32 = v.weights[..3].iter().sum();
                let w = if sum > 1e-6 { v.weights[..3].iter().map(|w| w / sum).collect::<Vec<_>>() } else { vec![1.0, 0.0, 0.0] };
                [w[0], w[1], w[2], 0.0]
            })
            .collect(),
        indices,
        material: MaterialDesc {
            base_color: [colour[0], colour[1], colour[2], 1.0],
            texture: part.texture.map(|t| t as u32),
            alpha: if part.blended {
                AlphaMode::Blend
            } else if part.alpha_ref > 0 {
                AlphaMode::Mask((part.alpha_ref as f32 / 255.0).max(0.02))
            } else {
                AlphaMode::Opaque
            },
            two_sided: part.two_sided,
            vertex_color: true,
            lighting: Lighting::Standard,
        },
    }
}

/// A mesh's triangles turned the other way to ours (front: counter-clockwise): most of them face away from their
/// vertices' normals.
fn wound_backwards(part: &t2::SkaterMesh) -> bool {
    let mut against = 0i64;
    for t in part.indices.chunks_exact(3) {
        let [a, b, c] = [0, 1, 2].map(|k| &part.vertices[t[k] as usize]);
        let (pa, pb, pc) = (Vec3::from_array(a.position), Vec3::from_array(b.position), Vec3::from_array(c.position));
        let geometric = (pb - pa).cross(pc - pa);
        let normal = Vec3::from_array(a.normal) + Vec3::from_array(b.normal) + Vec3::from_array(c.normal);
        against += if geometric.dot(normal) < 0.0 { 1 } else { -1 };
    }
    against > 0
}

/// How long the game's steps are: one frame of its main loop, at its 60 Hz.
pub const PERIOD: std::time::Duration = std::time::Duration::from_nanos(16_666_667);

/// The skater: the game's one model.
pub const SKATER: engine_core::ModelId = 0;

/// The game's pose after a step as the contract's root and joints (which model, its numbering, its cuts and its step
/// are the driver's: the game's frames are its steps). The runtime draws between steps.
pub fn pose(game: &t2::Pose, out: &mut Pose) {
    let to_ours = Mat4::from_scale(Vec3::splat(1.0 / t2::INCHES_PER_METRE)) * Mat4::from_translation(-crate::stage::OFFSET);
    let mut draw = game.draw;
    (draw[3], draw[7], draw[11], draw[15]) = (0.0, 0.0, 0.0, 1.0);
    out.root = to_ours * Mat4::from_cols_array(&draw);
    out.joints.clear();
    out.joints.extend(game.palette.iter().map(|m| {
        let mut m = *m;
        (m[3], m[7], m[11], m[15]) = (0.0, 0.0, 0.0, 1.0);
        Mat4::from_cols_array(&m)
    }));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vertex(p: [f32; 3], n: [f32; 3]) -> t2::ffi::Thug2Vertex {
        t2::ffi::Thug2Vertex { position: p, normal: n, color: 0x8040_2010, weights: [2.0, 1.0, 1.0, 5.0], ..Default::default() }
    }

    /// A part wound clockwise to its normals is turned; colours halved, weights of three made to sum to 1.
    #[test]
    fn a_part_is_turned_to_face_its_normals() {
        let down = [0.0, -1.0, 0.0];
        let part = t2::SkaterMesh {
            vertices: vec![vertex([0.0; 3], down), vertex([0.0, 0.0, 1.0], down), vertex([1.0, 0.0, 0.0], down)],
            indices: vec![0, 1, 2],
            color: [0.5, 0.25, 1.0, 0.5],
            alpha_ref: 64,
            ..Default::default()
        };
        // ((1-0) x (2-0) = +z x +x = +y: up, against the normals)
        let m = mesh(&part);
        assert_eq!(m.indices, [0, 2, 1]);
        assert_eq!(m.colors[0], [0.5, 0.25, 0.125, 1.0]);
        assert_eq!(m.weights[0], [0.5, 0.25, 0.25, 0.0]);
        assert_eq!(m.material.base_color, [1.0, 0.5, 1.0, 1.0]);
        assert_eq!(m.material.alpha, AlphaMode::Mask(64.0 / 255.0));
        assert!(mesh(&t2::SkaterMesh::default()).indices.is_empty());
    }

    /// The board: the flat, long parts under the skater, and the joints only they are skinned to; a leg reaching
    /// down to it is not the board, nor is the joint it shares with it.
    #[test]
    fn the_board_is_what_lies_flat_and_long_under_the_feet() {
        let at = |p: [f32; 3], joint: u8| t2::ffi::Thug2Vertex { position: p, bones: [joint, 0, 0, 0], weights: [1.0, 0.0, 0.0, 0.0], ..Default::default() };
        let part = |vertices: Vec<t2::ffi::Thug2Vertex>| t2::SkaterMesh { indices: vec![0, 1, 2], vertices, ..Default::default() };
        let deck = part(vec![at([-5.0, 4.0, -18.0], 46), at([5.0, 4.0, -18.0], 46), at([0.0, 5.0, 18.0], 48), at([1.0, 5.0, 18.0], 48)]);
        let wheels = part(vec![at([-5.0, 0.0, -12.0], 47), at([5.0, 0.0, 12.0], 49), at([0.0, 2.0, 12.0], 9)]);
        let leg = part(vec![at([0.0, 3.0, 0.0], 9), at([0.0, 30.0, 0.0], 8), at([1.0, 30.0, 0.0], 8)]);
        let shoe = part(vec![at([0.0, 3.0, -4.0], 9), at([0.0, 7.0, 6.0], 9), at([1.0, 5.0, 0.0], 9)]);
        let group = board(&[deck, wheels, leg, shoe]).expect("a board");
        assert_eq!(group.id, body::BOARD);
        assert_eq!(group.joints, [46, 47, 48, 49]);
        let Cast::Footprint { back, front, .. } = group.cast else { panic!("a footprint") };
        assert_eq!((back.joint, back.at.z), (46, -18.0));
        assert_eq!((front.joint, front.at.z), (48, 18.0));
        assert!(board(&[]).is_none());
    }

    /// The root carries the model from game units at the offset to metres; the joints are the palette, w fixed.
    #[test]
    fn a_pose_is_in_metres_with_its_w_column_fixed() {
        // (row-major for row vectors: the translation is the fourth row, the w column junk)
        let mut draw = Mat4::from_translation(crate::stage::OFFSET + Vec3::new(t2::INCHES_PER_METRE, 0.0, 0.0)).to_cols_array();
        (draw[3], draw[15]) = (9.0, 0.0);
        let game = t2::Pose { palette: vec![draw], draw };
        let mut out = Pose::default();
        pose(&game, &mut out);
        let at = out.root.transform_point3(Vec3::ZERO);
        assert!((at - Vec3::X).length() < 1e-3, "{at}");
        assert_eq!(out.joints.len(), 1);
        assert_eq!(out.joints[0].w_axis.w, 1.0);
    }
}
