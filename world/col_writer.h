// THUG2 collision (a level's .col file, version 10) made from triangles (world/col_writer.cpp).
#pragma once

#include <cstdint>
#include <vector>

namespace headless {

struct CollisionTriangle {
    float v[3][3];
    uint16_t flags;   // collision_format::FaceFlag: skatable 0x1, not skatable 0x2, wall (ridable) 0x4, vert 0x8...
    uint16_t terrain; // the surface's sound / sparks kind (0: the default)
};

// The file's bytes: the triangles in objects (checksums from `first_checksum` up) with their BSP trees.
std::vector<uint8_t> write_collision(const std::vector<CollisionTriangle>& triangles, uint32_t first_checksum);

} // namespace headless
