#pragma once

#include <switch.h>

namespace sphaira::audio {

enum class SoundEffect {
    Focus,
    Scroll,
    Limit,
    Startup,
    Install,
    Error,
    MAX,
};

Result Init();
void Exit();

Result PlaySoundEffect(SoundEffect effect);

} // namespace sphaira::audio
