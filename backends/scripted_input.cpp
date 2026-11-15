// The pad the harness drives: a script of buttons and sticks by frame (later); for now no pad at all.
#include "backends/backends.h"

#include "app/probe.h"
#include "engine/pad_input/input_source.h"

namespace headless {
namespace {

class ScriptedInput final : public input::Source {
public:
    // (once a frame, before the pads are read)
    void read_frame() override { probe_frame(); }
};

} // namespace

input::Source& scripted_input()
{
    static ScriptedInput source;
    return source;
}

} // namespace headless
