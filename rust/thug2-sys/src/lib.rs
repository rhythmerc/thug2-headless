//! The THUG2 engine (thug2-decomp's game code, run headless by thug2-headless) as a library its host steps one game
//! frame at a time: [`ffi`] is its C API (thug2-headless's app/thug2_engine.h), the rest a safe wrapper.
//!
//! The game runs on a thread of its own and takes turns with its host: [`Engine::step`] hands it a pad, lets it run
//! exactly one frame of its main loop (logic only: nothing is drawn, no sound is made) and returns the skater as
//! that frame left it. Nothing of the game runs between steps.
//!
//! It runs on the player's own copy of the game: [`boot`] is given the folder holding its `Data`. A process boots it
//! once; it can't be booted again after it ends.
//!
//! Units and axes are the game's: inches, y up, a heading of 0 facing +z and 90 facing +x. Steering left while facing
//! +z turns towards +x, as in a right-handed frame with y up, so a right-handed host's metres map onto it by scale
//! alone ([`INCHES_PER_METRE`]).
use std::ffi::{CString, c_char, c_int};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};

/// The C API, as app/thug2_engine.h declares it.
pub mod ffi {
    use std::ffi::{c_char, c_int};

    pub const THUG2_CROSS: u16 = 1 << 0;
    pub const THUG2_CIRCLE: u16 = 1 << 1;
    pub const THUG2_SQUARE: u16 = 1 << 2;
    pub const THUG2_TRIANGLE: u16 = 1 << 3;
    pub const THUG2_L1: u16 = 1 << 4;
    pub const THUG2_R1: u16 = 1 << 5;
    pub const THUG2_L2: u16 = 1 << 6;
    pub const THUG2_R2: u16 = 1 << 7;

    pub const THUG2_FACE_SKATABLE: u16 = 0x1;
    pub const THUG2_FACE_NOT_SKATABLE: u16 = 0x2;
    pub const THUG2_FACE_WALL: u16 = 0x4;
    pub const THUG2_FACE_VERT: u16 = 0x8;

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
    pub struct Thug2Pad {
        pub left_x: u8,
        pub left_y: u8,
        pub right_x: u8,
        pub right_y: u8,
        pub buttons: u16,
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default)]
    pub struct Thug2Skater {
        pub present: c_int,
        pub position: [f32; 3],
        pub velocity: [f32; 3],
        pub state: c_int,
        pub frame: u32,
        pub forward: [f32; 3],
        pub up: [f32; 3],
        pub balance_kind: c_int,
        pub balance: f32,
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    pub struct Thug2Triangle {
        pub v: [[f32; 3]; 3],
        pub flags: u16,
        pub terrain: u16,
    }

    unsafe extern "C" {
        pub fn thug2_boot(game_dir: *const c_char, argc: c_int, argv: *const *const c_char) -> c_int;
        pub fn thug2_step(pad: *const Thug2Pad, skater: *mut Thug2Skater) -> c_int;
        pub fn thug2_set_world(
            triangles: *const Thug2Triangle,
            count: u32,
            spawn: *const f32,
            heading_degrees: f32,
        ) -> c_int;
        pub fn thug2_teleport(spawn: *const f32, heading_degrees: f32) -> c_int;
        pub fn thug2_set_rails(points: *const [f32; 3], counts: *const u32, rail_count: u32) -> c_int;
        pub fn thug2_set_cache_dir(folder: *const c_char);
        pub fn thug2_quit();
        pub fn thug2_skater_meshes(out: *mut Thug2Mesh, max: u32) -> u32;
        pub fn thug2_skater_mesh_data(mesh: u32, vertices: *mut Thug2Vertex, indices: *mut u16) -> c_int;
        pub fn thug2_skater_texture(index: u32, out: *mut Thug2Texture, bits: *mut std::ffi::c_void, size: u32) -> u32;
        pub fn thug2_skater_pose(palette: *mut [f32; 16], max: u32, draw_matrix: *mut f32) -> u32;
        pub fn thug2_skater_placements() -> u64;
        pub fn thug2_sound_voices(out: *mut Thug2Voice, max: u32) -> u32;
        pub fn thug2_sound(sound: u32, out: *mut Thug2Sound, samples: *mut i16, count: u32) -> c_int;
        pub fn thug2_hud_enable(on: c_int);
        pub fn thug2_hud(
            draws: *mut Thug2HudDraw,
            max_draws: u32,
            vertices: *mut Thug2HudVertex,
            max_vertices: u32,
            vertex_count: *mut u32,
        ) -> u32;
        pub fn thug2_hud_texture(texture: i32, width: *mut u32, height: *mut u32, rgba: *mut u8, size: u32) -> c_int;
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    pub struct Thug2HudVertex {
        pub x: f32,
        pub y: f32,
        pub u: f32,
        pub v: f32,
        /// ARGB (Direct3D 9's).
        pub color: u32,
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    pub struct Thug2HudDraw {
        pub first_vertex: u32,
        pub vertex_count: u32,
        pub texture: i32,
        pub blend: u8,
        pub source_blend: u8,
        pub dest_blend: u8,
        pub colour_op: u8,
        pub alpha_op: u8,
        pub pad: [u8; 3],
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default)]
    pub struct Thug2Voice {
        pub voice: u32,
        pub sound: u32,
        pub starts: u32,
        pub looping: u8,
        pub pad: [u8; 3],
        pub volume: i32,
        pub pan: i32,
        pub frequency: u32,
        pub rate: u32,
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default)]
    pub struct Thug2Sound {
        pub channels: u16,
        pub pad: u16,
        pub rate: u32,
        pub samples: u32,
    }

    pub const THUG2_TEXTURE_DXT1: u32 = 1;
    pub const THUG2_TEXTURE_DXT5: u32 = 5;
    pub const THUG2_TEXTURE_BGRA8: u32 = 32;

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default)]
    pub struct Thug2Vertex {
        pub position: [f32; 3],
        pub normal: [f32; 3],
        pub color: u32,
        pub uv: [f32; 2],
        pub bones: [u8; 4],
        pub weights: [f32; 4],
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default)]
    pub struct Thug2Mesh {
        pub vertex_count: u32,
        pub index_count: u32,
        pub texture: i32,
        pub two_sided: u8,
        pub alpha_ref: u8,
        pub blended: u8,
        pub pad: u8,
        pub color: [f32; 4],
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default)]
    pub struct Thug2Texture {
        pub width: u32,
        pub height: u32,
        pub format: u32,
        pub bytes: u32,
    }
}

/// The skater's model as the game built it: meshes in their bind pose (game units, model space), skinned by up to
/// three bones a vertex, and the textures they draw with. A posed vertex is `sum(weight[i] * position * palette[bone[i]])`
/// in row vectors (see [`Pose`]).
#[derive(Clone, Debug, Default)]
pub struct SkaterModel {
    pub meshes: Vec<SkaterMesh>,
    pub textures: Vec<Texture>,
}

#[derive(Clone, Debug, Default)]
pub struct SkaterMesh {
    pub vertices: Vec<ffi::Thug2Vertex>,
    /// A triangle list.
    pub indices: Vec<u16>,
    /// Index into [`SkaterModel::textures`].
    pub texture: Option<usize>,
    pub two_sided: bool,
    /// 0: no alpha test.
    pub alpha_ref: u8,
    pub blended: bool,
    /// The first pass's colour (0.5: unchanged).
    pub color: [f32; 4],
}

/// A texture's top level, decoded to RGBA (8 bits a channel, rows top first).
#[derive(Clone, Debug, Default)]
pub struct Texture {
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
}

/// A voice the game is playing a sound on (see [`Engine::voices`]).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Voice {
    /// The same while the game keeps this voice's buffer.
    pub voice: u32,
    /// [`Engine::sound`]'s.
    pub sound: u32,
    /// Changes each time the game plays it again from its start.
    pub starts: u32,
    pub looping: bool,
    /// Its level, 0 (silent) to 1 (full): the sound's own, with its fall-off from the game's camera.
    pub gain: f32,
    /// The pan, -1 (left) to 1 (right), as the game's camera hears it.
    pub pan: f32,
    /// How fast it plays: 1 is as recorded.
    pub speed: f32,
}

/// A sound's samples: 16-bit PCM, interleaved by channel.
#[derive(Clone, Debug, Default)]
pub struct Sound {
    pub channels: u16,
    pub rate: u32,
    pub samples: Vec<i16>,
}

/// How a HUD draw's colour is made and laid over what is under it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum HudBlend {
    /// Over what's under it by its alpha (the usual).
    #[default]
    Alpha,
    /// Added to what's under it (by its alpha).
    Add,
    /// Drawn as it is (not blended).
    Opaque,
}

/// A draw of the game's 2D (see [`Engine::hud`]): a triangle list of screen vertices with one texture.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct HudDraw {
    /// Into [`Hud::vertices`]: three a triangle.
    pub first_vertex: u32,
    pub vertex_count: u32,
    /// [`Engine::hud_texture`]'s (an id kept while the texture lives; never reused); none: the colour alone.
    pub texture: Option<i32>,
    pub blend: HudBlend,
    /// Its colour is its texture's times its vertices', doubled (the game's 2D); otherwise just multiplied.
    pub doubled: bool,
}

/// A HUD vertex: the screen's pixels (640x480 from its top left), a texture coordinate and an RGBA colour (0-255).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct HudVertex {
    pub position: [f32; 2],
    pub uv: [f32; 2],
    pub color: [u8; 4],
}

/// A frame's 2D, in the order the game draws it.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Hud {
    pub draws: Vec<HudDraw>,
    pub vertices: Vec<HudVertex>,
}

/// The size of the screen the 2D is laid out on, pixels.
pub const HUD_SIZE: [f32; 2] = [640.0, 480.0];

/// Hundredths of a decibel (DirectSound's, -10000 silent ... 0 full) as a level, 0 to 1.
pub fn level_of(hundredths_db: i32) -> f32 {
    if hundredths_db <= -10000 { 0.0 } else { 10f32.powf(hundredths_db.min(0) as f32 / 2000.0) }
}

/// The skater's pose after a frame: its skin palette (a bone's inverse bind times its posed matrix, row-major, row
/// vectors) and its draw matrix (model to world, the same).
#[derive(Clone, Debug, Default)]
pub struct Pose {
    pub palette: Vec<[f32; 16]>,
    pub draw: [f32; 16],
}

/// Game units (inches) in a metre.
pub const INCHES_PER_METRE: f32 = 39.370_08;

/// A pad as the game reads one: sticks 0-255 (0x80 at rest; up and left are 0), buttons by bit ([`buttons`]).
pub type Pad = ffi::Thug2Pad;

/// A pad with nothing pressed.
pub const PAD_AT_REST: Pad = Pad { left_x: 0x80, left_y: 0x80, right_x: 0x80, right_y: 0x80, buttons: 0 };

/// The pad's buttons (a PlayStation 2 pad's, as the game names them).
pub mod buttons {
    pub use super::ffi::{
        THUG2_CIRCLE as CIRCLE, THUG2_CROSS as CROSS, THUG2_L1 as L1, THUG2_L2 as L2, THUG2_R1 as R1, THUG2_R2 as R2,
        THUG2_SQUARE as SQUARE, THUG2_TRIANGLE as TRIANGLE,
    };
}

/// A collision face's flags: what the skater may do on it.
pub mod face {
    pub use super::ffi::{
        THUG2_FACE_NOT_SKATABLE as NOT_SKATABLE, THUG2_FACE_SKATABLE as SKATABLE, THUG2_FACE_VERT as VERT,
        THUG2_FACE_WALL as WALL,
    };
}

/// A triangle of the host's world, game units. Its front (the side the skater rides on) is where
/// `(v[1] - v[0]) x (v[2] - v[0])` points: from behind, the skater passes through it.
pub type Triangle = ffi::Thug2Triangle;

/// What the skater is doing (its physics state).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State {
    Ground,
    Air,
    Wall,
    Lip,
    Rail,
    Wallplant,
    Unknown,
}

impl State {
    fn from_raw(raw: c_int) -> Self {
        match raw {
            0 => Self::Ground,
            1 => Self::Air,
            2 => Self::Wall,
            3 => Self::Lip,
            4 => Self::Rail,
            5 => Self::Wallplant,
            _ => Self::Unknown,
        }
    }
}

/// The local skater after a frame: game units, world space.
#[derive(Clone, Copy, Debug)]
pub struct Skater {
    pub position: [f32; 3],
    /// Inches a second.
    pub velocity: [f32; 3],
    /// Where it faces, and its up (unit vectors).
    pub forward: [f32; 3],
    pub up: [f32; 3],
    pub state: State,
    /// The game's frames so far.
    pub frame: u32,
    /// The balance trick being balanced, if any: its meter.
    pub balance: Option<Balance>,
}

/// A balance trick's meter: which, and its lean as a share of the lean the skater bails at (-1 to 1; holding left moves
/// it left, as the game's meter shows it).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Balance {
    pub kind: BalanceKind,
    pub lean: f32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BalanceKind {
    /// A grind or slide (the game's meter above the skater's head).
    Grind,
    /// A manual or nose manual (beside the skater).
    Manual,
    Lip,
    Skitch,
}

/// Where to put the skater: a point (it lands on the ground below) and the way it faces.
#[derive(Clone, Copy, Debug)]
pub struct Spawn {
    pub point: [f32; 3],
    pub heading_degrees: f32,
}

/// The heading (degrees, 0 facing +z, 90 facing +x) of a direction.
pub fn heading_of(direction: [f32; 3]) -> f32 {
    direction[0].atan2(direction[2]).to_degrees()
}

#[derive(Debug)]
pub enum Error {
    /// This process booted the engine already (it boots once).
    AlreadyBooted,
    /// A path or argument with a NUL in it.
    BadText,
    /// The game ended before its first frame (its data missing or unreadable, most likely).
    EndedAtBoot,
    /// The game has ended.
    Ended,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::AlreadyBooted => "THUG2 was already started in this run of the app",
            Self::BadText => "a path or argument has a NUL in it",
            Self::EndedAtBoot => "THUG2 ended before its first frame (is its game data complete?)",
            Self::Ended => "THUG2 has ended",
        })
    }
}

impl std::error::Error for Error {}

static BOOTED: AtomicBool = AtomicBool::new(false);

/// The running engine. There is one at most (a process boots the game once); it may be moved between threads, and
/// is used from one at a time.
pub struct Engine {
    ended: bool,
}

/// Starts the game from `game_dir` (the folder holding its `Data`) with the game's command line `args` (e.g.
/// `["-level", "Default"]`), the files it makes kept in `cache_dir`. Blocks until its first frame is ready (a
/// fraction of a second to a few seconds: it loads its scripts and the level).
pub fn boot(game_dir: &Path, cache_dir: Option<&Path>, args: &[&str]) -> Result<Engine, Error> {
    if BOOTED.swap(true, Ordering::SeqCst) {
        return Err(Error::AlreadyBooted);
    }
    let text = |s: &str| CString::new(s).map_err(|_| Error::BadText);
    let dir = text(&game_dir.to_string_lossy())?;
    if let Some(cache) = cache_dir {
        let cache = text(&cache.to_string_lossy())?;
        // SAFETY: a NUL-terminated string the call copies.
        unsafe { ffi::thug2_set_cache_dir(cache.as_ptr()) };
    }
    let words: Vec<CString> = std::iter::once("thug2").chain(args.iter().copied()).map(text).collect::<Result<_, _>>()?;
    let pointers: Vec<*const c_char> = words.iter().map(|w| w.as_ptr()).collect();
    // SAFETY: NUL-terminated strings the call copies (the game keeps its own copy of the command line).
    let result = unsafe { ffi::thug2_boot(dir.as_ptr(), pointers.len() as c_int, pointers.as_ptr()) };
    if result != 0 {
        return Err(Error::EndedAtBoot);
    }
    Ok(Engine { ended: false })
}

impl Engine {
    /// One game frame with `pad`. The skater as the frame left it (`None` before it is in the level).
    pub fn step(&mut self, pad: &Pad) -> Result<Option<Skater>, Error> {
        if self.ended {
            return Err(Error::Ended);
        }
        let mut raw = ffi::Thug2Skater::default();
        // SAFETY: both point at live values for the call's length.
        if unsafe { ffi::thug2_step(pad, &mut raw) } != 0 {
            self.ended = true;
            return Err(Error::Ended);
        }
        Ok((raw.present != 0).then(|| Skater {
            position: raw.position,
            velocity: raw.velocity,
            forward: raw.forward,
            up: raw.up,
            state: State::from_raw(raw.state),
            frame: raw.frame,
            balance: match raw.balance_kind {
                1 => Some(BalanceKind::Grind),
                2 => Some(BalanceKind::Manual),
                3 => Some(BalanceKind::Lip),
                4 => Some(BalanceKind::Skitch),
                _ => None,
            }
            .map(|kind| Balance { kind, lean: raw.balance }),
        }))
    }

    /// The host's world as collision the skater rides (replacing the last world given), from the next step on (once
    /// the skater is in the level). With `spawn` the skater is put there on its board; without, put back on its
    /// board where it is (still, either way).
    pub fn set_world(&mut self, triangles: &[Triangle], spawn: Option<Spawn>) {
        if triangles.is_empty() {
            return;
        }
        let (point, heading) = spawn.map_or((None, 0.0), |s| (Some(s.point), s.heading_degrees));
        // SAFETY: the triangles are copied by the call; `point` lives until it returns.
        unsafe {
            ffi::thug2_set_world(
                triangles.as_ptr(),
                triangles.len() as u32,
                point.as_ref().map_or(std::ptr::null(), |p| p.as_ptr()),
                heading,
            );
        }
    }

    /// The host's rails in place of the level's (each a polyline of two points or more), from the next step on.
    pub fn set_rails(&mut self, rails: &[Vec<[f32; 3]>]) {
        let points: Vec<[f32; 3]> = rails.iter().flatten().copied().collect();
        let counts: Vec<u32> = rails.iter().map(|r| r.len() as u32).collect();
        // SAFETY: both arrays are copied by the call.
        unsafe { ffi::thug2_set_rails(points.as_ptr(), counts.as_ptr(), counts.len() as u32) };
    }

    /// The skater put on its board at `spawn` (on the ground below it), still, from the next step on.
    pub fn teleport(&mut self, spawn: Spawn) {
        // SAFETY: three floats, read during the call.
        unsafe { ffi::thug2_teleport(spawn.point.as_ptr(), spawn.heading_degrees) };
    }

    /// The skater's model (`None`: no skater, or its model isn't built yet).
    pub fn skater_model(&mut self) -> Option<SkaterModel> {
        // SAFETY: a count query (no buffer), then buffers as long as the counts it gave.
        unsafe {
            let count = ffi::thug2_skater_meshes(std::ptr::null_mut(), 0);
            if count == 0 {
                return None;
            }
            let mut raw = vec![ffi::Thug2Mesh::default(); count as usize];
            ffi::thug2_skater_meshes(raw.as_mut_ptr(), count);
            let mut meshes = Vec::with_capacity(raw.len());
            for (i, m) in raw.iter().enumerate() {
                let mut vertices = vec![ffi::Thug2Vertex::default(); m.vertex_count as usize];
                let mut indices = vec![0u16; m.index_count as usize];
                if ffi::thug2_skater_mesh_data(i as u32, vertices.as_mut_ptr(), indices.as_mut_ptr()) != 0 {
                    continue;
                }
                meshes.push(SkaterMesh {
                    vertices,
                    indices,
                    texture: usize::try_from(m.texture).ok(),
                    two_sided: m.two_sided != 0,
                    alpha_ref: m.alpha_ref,
                    blended: m.blended != 0,
                    color: m.color,
                });
            }
            let texture_count = ffi::thug2_skater_texture(u32::MAX, std::ptr::null_mut(), std::ptr::null_mut(), 0);
            let mut textures = Vec::with_capacity(texture_count as usize);
            for i in 0..texture_count {
                let mut info = ffi::Thug2Texture::default();
                ffi::thug2_skater_texture(i, &mut info, std::ptr::null_mut(), 0);
                let mut bits = vec![0u8; info.bytes as usize];
                ffi::thug2_skater_texture(i, &mut info, bits.as_mut_ptr().cast(), info.bytes);
                textures.push(decode_texture(&info, &bits));
            }
            Some(SkaterModel { meshes, textures })
        }
    }

    /// The skater's pose as the last step left it into `pose`; false if there's no skater.
    pub fn skater_pose(&mut self, pose: &mut Pose) -> bool {
        pose.palette.resize(64, [0.0; 16]);
        // SAFETY: 64 matrices and 16 floats, as told.
        let bones = unsafe { ffi::thug2_skater_pose(pose.palette.as_mut_ptr(), 64, pose.draw.as_mut_ptr()) };
        pose.palette.truncate(bones.min(64) as usize);
        bones > 0
    }

    /// How many times the game has put the skater somewhere outside its motion so far (only goes up): teleported or
    /// respawned (by [`Engine::teleport`] and [`Engine::set_world`] too), placed by a wall plant (backed off the wall
    /// to wait for a late press, put back for one, set off it turned its new way), turned round at once (rolling
    /// backwards, a turn-around) or its animations mirrored at once. A step that changes it is a jump in the skater's
    /// pose: nothing is drawn blended across it.
    pub fn skater_placements(&mut self) -> u64 {
        // SAFETY: no arguments; read between steps.
        unsafe { ffi::thug2_skater_placements() }
    }

    /// The voices the game is playing sounds on, as the last step left them, into `out`.
    pub fn voices(&mut self, out: &mut Vec<Voice>) {
        out.clear();
        // SAFETY: a count only.
        let count = unsafe { ffi::thug2_sound_voices(std::ptr::null_mut(), 0) };
        let mut raw = vec![ffi::Thug2Voice::default(); count as usize];
        // SAFETY: room for `count`.
        let count = unsafe { ffi::thug2_sound_voices(raw.as_mut_ptr(), count) }.min(raw.len() as u32);
        out.extend(raw[..count as usize].iter().map(|v| Voice {
            voice: v.voice,
            sound: v.sound,
            starts: v.starts,
            looping: v.looping != 0,
            gain: level_of(v.volume),
            // (the quieter side's cut, as a balance)
            pan: (1.0 - level_of(-v.pan.abs())) * v.pan.signum() as f32,
            speed: if v.rate > 0 { v.frequency as f32 / v.rate as f32 } else { 1.0 },
        }));
    }

    /// Sound `sound`'s samples (none: there's no such sound).
    pub fn sound(&mut self, sound: u32) -> Option<Sound> {
        let mut info = ffi::Thug2Sound::default();
        // SAFETY: a description only.
        if unsafe { ffi::thug2_sound(sound, &mut info, std::ptr::null_mut(), 0) } != 0 {
            return None;
        }
        let mut samples = vec![0i16; info.samples as usize];
        // SAFETY: room for its samples.
        unsafe { ffi::thug2_sound(sound, std::ptr::null_mut(), samples.as_mut_ptr(), info.samples) };
        Some(Sound { channels: info.channels.max(1), rate: info.rate, samples })
    }

    /// The game's 2D (its HUD) taken down from the next step on, or not (it costs the game's thread drawing it).
    pub fn hud_enable(&mut self, on: bool) {
        // SAFETY: a flag.
        unsafe { ffi::thug2_hud_enable(on as std::ffi::c_int) };
    }

    /// The last step's 2D (none taken down: [`Engine::hud_enable`]), into `out`.
    pub fn hud(&mut self, out: &mut Hud) {
        out.draws.clear();
        out.vertices.clear();
        let mut vertex_count = 0u32;
        // SAFETY: counts only.
        let count = unsafe { ffi::thug2_hud(std::ptr::null_mut(), 0, std::ptr::null_mut(), 0, &mut vertex_count) };
        let mut draws = vec![ffi::Thug2HudDraw::default(); count as usize];
        let mut vertices = vec![ffi::Thug2HudVertex::default(); vertex_count as usize];
        // SAFETY: room for both, as counted (the game is waiting for its turn: they can't have changed).
        unsafe { ffi::thug2_hud(draws.as_mut_ptr(), count, vertices.as_mut_ptr(), vertex_count, std::ptr::null_mut()) };
        out.draws.extend(draws.iter().map(|d| HudDraw {
            first_vertex: d.first_vertex,
            vertex_count: d.vertex_count,
            texture: (d.texture >= 0 && d.colour_op != 3).then_some(d.texture),
            blend: match (d.blend, d.source_blend, d.dest_blend) {
                (0, ..) => HudBlend::Opaque,
                (_, _, 2) => HudBlend::Add,
                _ => HudBlend::Alpha,
            },
            doubled: d.colour_op == 5,
        }));
        out.vertices.extend(vertices.iter().map(|v| HudVertex {
            position: [v.x, v.y],
            uv: [v.u, v.v],
            color: [(v.color >> 16) as u8, (v.color >> 8) as u8, v.color as u8, (v.color >> 24) as u8],
        }));
    }

    /// A HUD texture's top level (none: it's been released, or can't be read).
    pub fn hud_texture(&mut self, texture: i32) -> Option<Texture> {
        let (mut width, mut height) = (0u32, 0u32);
        // SAFETY: its size only.
        if unsafe { ffi::thug2_hud_texture(texture, &mut width, &mut height, std::ptr::null_mut(), 0) } != 0 {
            return None;
        }
        let mut rgba = vec![0u8; width as usize * height as usize * 4];
        // SAFETY: room for its texels.
        if unsafe { ffi::thug2_hud_texture(texture, &mut width, &mut height, rgba.as_mut_ptr(), rgba.len() as u32) } != 0 {
            return None;
        }
        Some(Texture { width, height, rgba })
    }

    /// Ends the game (its thread is left waiting: the process can't boot it again).
    pub fn quit(mut self) {
        self.ended = true;
        // SAFETY: no arguments; safe at any time.
        unsafe { ffi::thug2_quit() };
    }
}

/// A texture's top level as RGBA: DXT1 / DXT5 blocks decoded, BGRA swizzled; a format the host can't read is grey.
fn decode_texture(info: &ffi::Thug2Texture, bits: &[u8]) -> Texture {
    let (w, h) = (info.width.max(1), info.height.max(1));
    let mut rgba = vec![128u8; (w * h * 4) as usize];
    match info.format {
        ffi::THUG2_TEXTURE_BGRA8 => {
            for (o, i) in rgba.chunks_exact_mut(4).zip(bits.chunks_exact(4)) {
                o.copy_from_slice(&[i[2], i[1], i[0], i[3]]);
            }
        }
        ffi::THUG2_TEXTURE_DXT1 | ffi::THUG2_TEXTURE_DXT5 => {
            let dxt5 = info.format == ffi::THUG2_TEXTURE_DXT5;
            let block_bytes = if dxt5 { 16 } else { 8 };
            let (bw, bh) = (w.div_ceil(4), h.div_ceil(4));
            for by in 0..bh {
                for bx in 0..bw {
                    let at = ((by * bw + bx) * block_bytes) as usize;
                    let Some(block) = bits.get(at..at + block_bytes as usize) else { continue };
                    let texels = decode_block(block, dxt5);
                    for (k, texel) in texels.iter().enumerate() {
                        let (x, y) = (bx * 4 + k as u32 % 4, by * 4 + k as u32 / 4);
                        if x < w && y < h {
                            let o = ((y * w + x) * 4) as usize;
                            rgba[o..o + 4].copy_from_slice(texel);
                        }
                    }
                }
            }
        }
        _ => {}
    }
    Texture { width: w, height: h, rgba }
}

/// One 4x4 block (DXT1: 8 bytes; DXT5: 8 bytes of alpha, then the colour block), its 16 texels row by row.
fn decode_block(block: &[u8], dxt5: bool) -> [[u8; 4]; 16] {
    let (alpha, colour) = if dxt5 { (Some(&block[..8]), &block[8..16]) } else { (None, &block[..8]) };
    let c0 = u16::from_le_bytes([colour[0], colour[1]]);
    let c1 = u16::from_le_bytes([colour[2], colour[3]]);
    let rgb = |c: u16| {
        let (r, g, b) = ((c >> 11) & 31, (c >> 5) & 63, c & 31);
        [(r * 255 / 31) as u16, (g * 255 / 63) as u16, (b * 255 / 31) as u16]
    };
    let (a, b) = (rgb(c0), rgb(c1));
    let mix = |x: [u16; 3], y: [u16; 3], wx: u16, wy: u16| {
        [0, 1, 2].map(|i| ((x[i] * wx + y[i] * wy) / (wx + wy)) as u8)
    };
    let four_colour = c0 > c1 || dxt5;
    let palette: [[u8; 4]; 4] = [
        [a[0] as u8, a[1] as u8, a[2] as u8, 255],
        [b[0] as u8, b[1] as u8, b[2] as u8, 255],
        if four_colour { with_alpha(mix(a, b, 2, 1)) } else { with_alpha(mix(a, b, 1, 1)) },
        if four_colour { with_alpha(mix(a, b, 1, 2)) } else { [0, 0, 0, 0] },
    ];
    let indices = u32::from_le_bytes([colour[4], colour[5], colour[6], colour[7]]);
    let mut out = [[0u8; 4]; 16];
    for (k, texel) in out.iter_mut().enumerate() {
        *texel = palette[((indices >> (2 * k)) & 3) as usize];
    }
    if let Some(alpha) = alpha {
        let (a0, a1) = (alpha[0] as u32, alpha[1] as u32);
        let mut levels = [0u32; 8];
        levels[0] = a0;
        levels[1] = a1;
        for i in 2..8 {
            levels[i] = if a0 > a1 {
                (a0 * (8 - i as u32) + a1 * (i as u32 - 1)) / 7
            } else if i < 6 {
                (a0 * (6 - i as u32) + a1 * (i as u32 - 1)) / 5
            } else if i == 6 {
                0
            } else {
                255
            };
        }
        let bits = alpha[2..8].iter().rev().fold(0u64, |acc, &b| (acc << 8) | b as u64);
        for (k, texel) in out.iter_mut().enumerate() {
            texel[3] = levels[((bits >> (3 * k)) & 7) as usize] as u8;
        }
    }
    out
}

fn with_alpha(c: [u8; 3]) -> [u8; 4] {
    [c[0], c[1], c[2], 255]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn heading_follows_the_games_axes() {
        assert!((heading_of([0.0, 0.0, 1.0]) - 0.0).abs() < 1e-4);
        assert!((heading_of([1.0, 0.0, 0.0]) - 90.0).abs() < 1e-4);
        assert!((heading_of([-1.0, 0.0, 0.0]) + 90.0).abs() < 1e-4);
    }

    #[test]
    fn dxt_blocks_decode() {
        // DXT1: colour 0 pure red, colour 1 pure blue; texel 0 colour 0, texel 1 colour 1, texel 2 two thirds red.
        let block = [0x00, 0xF8, 0x1F, 0x00, 0b10_01_00, 0, 0, 0];
        let t = decode_block(&block, false);
        assert_eq!(t[0], [255, 0, 0, 255]);
        assert_eq!(t[1], [0, 0, 255, 255]);
        assert_eq!(t[2], [170, 0, 85, 255]);
        // DXT5: alpha 255 and 0, every texel's index 1 (alpha 0) but the last's 0 (alpha 255).
        let indices: u64 = (0..15).map(|k| 1u64 << (3 * k)).sum();
        let mut full = vec![255u8, 0];
        full.extend_from_slice(&indices.to_le_bytes()[..6]);
        full.extend_from_slice(&block);
        let t5 = decode_block(&full, true);
        assert_eq!(t5[0][3], 0);
        assert_eq!(t5[15][3], 255);
        assert_eq!(&t5[0][..3], &[255, 0, 0]);
    }

    #[test]
    fn layouts_match_the_header() {
        assert_eq!(std::mem::size_of::<ffi::Thug2Vertex>(), 12 + 12 + 4 + 8 + 4 + 16);
        assert_eq!(std::mem::size_of::<ffi::Thug2Mesh>(), 4 * 3 + 4 + 16);
        assert_eq!(std::mem::size_of::<ffi::Thug2Texture>(), 16);
        assert_eq!(std::mem::size_of::<ffi::Thug2Pad>(), 6);
        assert_eq!(std::mem::size_of::<ffi::Thug2Triangle>(), 40);
        assert_eq!(std::mem::size_of::<ffi::Thug2Skater>(), 4 + 12 + 12 + 4 + 4 + 12 + 12 + 4 + 4);
        assert_eq!(std::mem::size_of::<ffi::Thug2Voice>(), 4 * 3 + 4 + 4 * 4);
        assert_eq!(std::mem::size_of::<ffi::Thug2Sound>(), 12);
        assert_eq!(std::mem::size_of::<ffi::Thug2HudVertex>(), 20);
        assert_eq!(std::mem::size_of::<ffi::Thug2HudDraw>(), 20);
    }

    #[test]
    fn decibels_are_levels() {
        assert_eq!(level_of(0), 1.0);
        assert_eq!(level_of(-10000), 0.0);
        assert!((level_of(-600) - 0.501).abs() < 0.01, "-6 dB is half");
    }

    /// The engine on the player's own game data (`THUG2_GAME_DIR`: the folder holding `Data`; skipped without it):
    /// a floor and a rail given, the skater pushes along the floor.
    #[test]
    fn skates_a_host_floor() {
        let Some(dir) = std::env::var_os("THUG2_GAME_DIR") else {
            eprintln!("THUG2_GAME_DIR not set: skipped");
            return;
        };
        let cache = std::env::temp_dir().join("thug2-sys-test-cache");
        let mut engine = boot(Path::new(&dir), Some(&cache), &["-level", "Default"]).expect("boot");
        let mut skater = None;
        for _ in 0..120 {
            skater = engine.step(&PAD_AT_REST).expect("step");
        }
        assert!(skater.is_some(), "the skater is in the level by frame 120");
        let (cx, cz) = (50_000.0, 50_000.0);
        let quad = |a: [f32; 3], b: [f32; 3], c: [f32; 3], d: [f32; 3]| {
            [Triangle { v: [a, b, c], flags: face::SKATABLE, terrain: 0 }, Triangle { v: [a, c, d], flags: face::SKATABLE, terrain: 0 }]
        };
        let floor = quad([cx - 400.0, 0.0, cz - 400.0], [cx - 400.0, 0.0, cz + 400.0], [cx + 400.0, 0.0, cz + 400.0], [cx + 400.0, 0.0, cz - 400.0]);
        engine.set_world(&floor, Some(Spawn { point: [cx, 20.0, cz - 300.0], heading_degrees: 0.0 }));
        engine.set_rails(&[vec![[cx + 100.0, 30.0, cz], [cx + 100.0, 30.0, cz + 100.0]]]);
        let start = engine.step(&PAD_AT_REST).unwrap().unwrap();
        assert!((start.position[2] - (cz - 300.0)).abs() < 1.0, "put at the spawn: {start:?}");
        assert!(start.position[1].abs() < 0.1, "on the floor: {start:?}");
        let push = Pad { left_y: 0, ..PAD_AT_REST };
        let mut last = start;
        for _ in 0..60 {
            last = engine.step(&push).unwrap().unwrap();
        }
        assert!(last.position[2] > start.position[2] + 50.0, "pushed along +z: {last:?}");
        assert_eq!(last.state, State::Ground);
        assert!(last.forward[2] > 0.9, "facing +z: {last:?}");
        // The skater's look: its model and a pose.
        let model = engine.skater_model().expect("the skater's model");
        assert!(model.meshes.len() > 10 && !model.textures.is_empty(), "{} meshes", model.meshes.len());
        let mesh = &model.meshes[0];
        assert!(mesh.indices.iter().all(|&i| (i as usize) < mesh.vertices.len()));
        let mut pose = Pose::default();
        assert!(engine.skater_pose(&mut pose));
        assert!(pose.palette.len() > 20, "{} bones", pose.palette.len());
        assert!(model.meshes.iter().flat_map(|m| &m.vertices).all(|v| v.bones[..3].iter().all(|&b| (b as usize) < pose.palette.len())));
        assert!((pose.draw[13] - last.position[1]).abs() < 1.0 && (pose.draw[14] - last.position[2]).abs() < 1.0, "placed at the skater");
        // Its sound: rolling along, a looping voice with its samples.
        let mut voices = Vec::new();
        engine.voices(&mut voices);
        let rolling = voices.iter().find(|v| v.looping).expect("the rolling loop plays");
        assert!(rolling.gain > 0.0 && rolling.gain <= 1.0 && rolling.speed > 0.5 && rolling.speed < 2.0, "{rolling:?}");
        let sound = engine.sound(rolling.sound).expect("its samples");
        assert!(sound.rate > 8000 && sound.samples.len() > sound.rate as usize / 10, "{} samples at {}", sound.samples.len(), sound.rate);
        assert!(sound.samples.iter().any(|&s| s != 0));
        // Its HUD, asked for: the score panel and the rest, on the 640x480 screen, with their textures.
        engine.hud_enable(true);
        engine.step(&push).unwrap();
        let mut hud = Hud::default();
        engine.hud(&mut hud);
        assert!(hud.draws.len() >= 3, "{} draws", hud.draws.len());
        assert!(hud.draws.iter().all(|d| (d.first_vertex + d.vertex_count) as usize <= hud.vertices.len() && d.vertex_count % 3 == 0));
        assert!(hud.vertices.iter().all(|v| v.position.iter().all(|p| p.is_finite())));
        let on_screen = hud.vertices.iter().filter(|v| (0.0..=640.0).contains(&v.position[0]) && (0.0..=480.0).contains(&v.position[1]));
        assert!(on_screen.count() * 2 > hud.vertices.len(), "laid out on the screen");
        let texture = hud.draws.iter().find_map(|d| d.texture).expect("a textured draw");
        let texels = engine.hud_texture(texture).expect("its texture");
        assert_eq!(texels.rgba.len(), (texels.width * texels.height * 4) as usize);
        assert!(texels.rgba.chunks(4).any(|t| t[3] > 0));
        engine.hud_enable(false);
        engine.step(&push).unwrap();
        engine.hud(&mut hud);
        assert!(hud.draws.is_empty(), "not taken down once off");
        assert!(matches!(boot(Path::new(&dir), None, &[]), Err(Error::AlreadyBooted)));
    }
}
