// sokol_gfx declarations and the generated shaders, with warnings off for third-party and
// generated code. The backend (SOKOL_METAL or SOKOL_GLES3) is set by CMake.
#pragma once

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// clang-format off: the generated shader header must come after sokol_gfx.h.
#include <sokol_gfx.h>
#include <sokol_log.h>
#include "shaders/viewport.glsl.h"
// clang-format on

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
