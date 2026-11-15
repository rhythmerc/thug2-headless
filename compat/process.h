#pragma once
#include <cstdio>
// _cexit: the C runtime's exit functions run without ending the process (here: the streams flushed).
inline void _cexit() { std::fflush(nullptr); }
