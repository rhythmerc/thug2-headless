#pragma once
#include "app/thug2_engine.h"
namespace headless {
// The local skater as the last frame left it (app/probe.cpp). Read while the game waits for its turn.
void read_skater(Thug2Skater* out);
// The pad the host gave this frame (app/engine.cpp).
Thug2Pad current_pad();
} // namespace headless
