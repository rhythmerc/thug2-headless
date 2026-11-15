/* The THUG2 engine, stepped by its host one game frame at a time (the C API skate3-quest-ar calls).
 *
 * thug2_boot starts the game on a thread of its own and returns once the game's main loop is ready for its first
 * frame. After that the game and its host take turns, never running together: thug2_step hands the game a pad, lets it
 * run exactly one frame of its main loop (logic only, flat out: no 60 Hz wait, nothing drawn), and returns once the
 * game is back at the top of its loop - the skater as that frame left it. The handover is the game's own loop pacing
 * hook (an extension's loop_pace): the decomp's main loop is not changed. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A pad as the game reads one: sticks 0-255 (0x80 at rest; up and left are 0), buttons by bit. */
enum {
    THUG2_CROSS = 1 << 0,
    THUG2_CIRCLE = 1 << 1,
    THUG2_SQUARE = 1 << 2,
    THUG2_TRIANGLE = 1 << 3,
    THUG2_L1 = 1 << 4,
    THUG2_R1 = 1 << 5,
    THUG2_L2 = 1 << 6,
    THUG2_R2 = 1 << 7,
};
typedef struct Thug2Pad {
    uint8_t left_x, left_y, right_x, right_y;
    uint16_t buttons;
} Thug2Pad;

/* The local skater after a frame. state: 0 ground, 1 air, 2 wall, 3 lip, 4 rail, 5 wallplant, -1 unknown. Game units
 * (inches), world space. forward / up: its orientation (unit rows of its basis frame: where it faces, its up).
 * balance_kind: the balance trick being balanced (its meter's): THUG2_BALANCE_*; balance: its lean as a share of the
 * lean it bails at (-1 to 1), as the game's meter shows it: holding left moves it left (negative). */
enum {
    THUG2_BALANCE_NONE = 0,
    THUG2_BALANCE_GRIND = 1, /* or a slide */
    THUG2_BALANCE_MANUAL = 2, /* or a nose manual */
    THUG2_BALANCE_LIP = 3,
    THUG2_BALANCE_SKITCH = 4,
};
typedef struct Thug2Skater {
    int present;
    float position[3];
    float velocity[3];
    int state;
    uint32_t frame;
    float forward[3];
    float up[3];
    int balance_kind;
    float balance;
} Thug2Skater;

/* A triangle of the host's world, game units, with its face flags (THUG2's collision flags). Its front - the side
 * the skater rides on - is where (v[1] - v[0]) x (v[2] - v[0]) points: a face wound the other way is passed through. */
enum {
    THUG2_FACE_SKATABLE = 0x1,
    THUG2_FACE_NOT_SKATABLE = 0x2,
    THUG2_FACE_WALL = 0x4, /* wallridable */
    THUG2_FACE_VERT = 0x8, /* a quarter pipe's vertical part: the skater goes straight up and lands back in */
};
typedef struct Thug2Triangle {
    float v[3][3];
    uint16_t flags;
    uint16_t terrain;
} Thug2Triangle;

/* Boots the game from `game_dir` (the folder holding Data/) with the game's command line (e.g. "-level", "BO").
 * Returns 0 when the first frame is ready; non-zero if the game ended before it got there. */
int thug2_boot(const char* game_dir, int argc, const char* const* argv);
/* One frame with `pad` (null: at rest); `skater` (may be null) as the frame left it. Returns 0, or non-zero once the
 * game has ended. */
int thug2_step(const Thug2Pad* pad, Thug2Skater* skater);
/* The host's world as collision the game skates (a scene of its own, added beside the level's; a world given again
 * replaces the last one), and the skater put on its board at `spawn` facing `heading_degrees` (0: +z, 90: +x) - or,
 * with `spawn` null, put back on its board where it is, facing the way it faces (still: its momentum is lost). Taken
 * at the start of the next step, once the skater is in the level. Returns 0 if the triangles were taken (it copies
 * them). */
int thug2_set_world(const Thug2Triangle* triangles, uint32_t count, const float spawn[3], float heading_degrees);
/* The skater put on its board at `spawn` (on the ground below it) facing `heading_degrees` (0: +z, 90: +x), still.
 * Taken at the start of the next step (after a world given with it). Returns 0. */
int thug2_teleport(const float spawn[3], float heading_degrees);
/* Where the game keeps files it makes (call before thug2_boot; null: $XDG_CACHE_HOME/thug2-headless). */
void thug2_set_cache_dir(const char* folder);
/* The host's rails, in place of the level's: `rail_count` polylines, rail i being counts[i] points (x, y, z, game
 * units) taken in turn from `points`. Grinding works on them as on a level's rails; they run no level scripts.
 * Taken with the world (at the start of the next step once the skater is in the level). Returns 0 if taken. */
int thug2_set_rails(const float (*points)[3], const uint32_t* counts, uint32_t rail_count);
/* Asks the game to end (its next frame quits the loop). */
void thug2_quit(void);

/* --- The skater's look: its skinned model and its pose, for a host that draws it. Read between steps. ---
 *
 * The model is the skater's meshes in its bind pose (game units, model space) with up to three bones a vertex. A
 * vertex's place in a frame is sum(weight[i] * position * palette[bone[i]]) - row vectors, as the game skins: the
 * palette is each bone's inverse bind times its posed matrix. The model is placed in the world by the draw matrix
 * (row vectors too; the skater's regular / goofy mirroring is in it). */
typedef struct Thug2Vertex {
    float position[3];
    float normal[3];
    uint32_t color; /* BGRA bytes (0x80: unchanged) */
    float uv[2];
    uint8_t bones[4];
    float weights[4];
} Thug2Vertex;

enum {
    THUG2_TEXTURE_DXT1 = 1,
    THUG2_TEXTURE_DXT5 = 5,
    THUG2_TEXTURE_BGRA8 = 32,
};
typedef struct Thug2Mesh {
    uint32_t vertex_count;
    uint32_t index_count; /* a triangle list: three a triangle */
    int32_t texture;      /* the texture (thug2_skater_texture's index) its first pass draws with; -1: none */
    uint8_t two_sided;
    uint8_t alpha_ref;    /* 0: no alpha test */
    uint8_t blended;      /* drawn blended (its first pass isn't opaque) */
    uint8_t pad;
    float color[4];       /* its first pass's colour (0.5: unchanged) */
} Thug2Mesh;
typedef struct Thug2Texture {
    uint32_t width, height;
    uint32_t format; /* THUG2_TEXTURE_*; 0: one the host can't use */
    uint32_t bytes;  /* the top level's */
} Thug2Texture;

/* The skater's meshes into `out` (at most `max`); the number there are (0: no skater, or its model isn't built). */
uint32_t thug2_skater_meshes(Thug2Mesh* out, uint32_t max);
/* Mesh `mesh`'s vertices and indices into arrays as long as its counts. 0, or non-zero if there's no such mesh. */
int thug2_skater_mesh_data(uint32_t mesh, Thug2Vertex* vertices, uint16_t* indices);
/* The textures the meshes use: texture `index`'s description, and (with `bits`, `size` its bytes) its top level. The
 * number of textures. */
uint32_t thug2_skater_texture(uint32_t index, Thug2Texture* out, void* bits, uint32_t size);
/* The skater's pose: its palette (a 4x4 matrix a bone, row-major, at most `max`) and its draw matrix. The number of
 * bones (0: no skater). */
uint32_t thug2_skater_pose(float (*palette)[16], uint32_t max, float draw_matrix[16]);

#ifdef __cplusplus
}
#endif
