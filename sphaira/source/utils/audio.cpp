#include "utils/audio.hpp"
#include "utils/profile.hpp"
#include "utils/thread.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <pulsar.h>
#include <atomic>
#include <cstring>
#include <utility>

namespace sphaira::audio {
namespace {

Mutex g_mutex{};
PLSR_PlayerSoundId g_sound_ids[std::to_underlying(SoundEffect::MAX)]{};
std::atomic_bool g_is_init{};

} // namespace

Result Init() {
    if (g_is_init) {
        R_SUCCEED();
    }

    SCOPED_MUTEX(&g_mutex);
    R_TRY(plsrPlayerInit());

    if (R_SUCCEEDED(romfsMountDataStorageFromProgram(0x0100000000001000, "qlaunch"))) {
        ON_SCOPE_EXIT(romfsUnmount("qlaunch"));

        PLSR_BFSAR qlaunch_bfsar;
        if (R_SUCCEEDED(plsrBFSAROpen("qlaunch:/sound/qlaunch.bfsar", &qlaunch_bfsar))) {
            ON_SCOPE_EXIT(plsrBFSARClose(&qlaunch_bfsar));

            const auto load_sound = [&qlaunch_bfsar](const char* name, SoundEffect id) {
                if (R_FAILED(plsrPlayerLoadSoundByName(&qlaunch_bfsar, name, &g_sound_ids[std::to_underlying(id)]))) {
                    log_write("[PLSR] failed to load sound effect: %s\n", name);
                }
            };

            // 23ms
            {
                SCOPED_TIMESTAMP("audio sound effect load");
                load_sound("SeGameIconFocus", SoundEffect::Focus);
                load_sound("SeGameIconScroll", SoundEffect::Scroll);
                load_sound("SeGameIconLimit", SoundEffect::Limit);
                load_sound("StartupMenu_Game", SoundEffect::Startup);
                load_sound("SeGameIconAdd", SoundEffect::Install);
                load_sound("SeInsertError", SoundEffect::Error);
            }

            plsrPlayerSetVolume(g_sound_ids[std::to_underlying(SoundEffect::Limit)], 2.0f);
            plsrPlayerSetVolume(g_sound_ids[std::to_underlying(SoundEffect::Focus)], 0.5f);
        }
    } else {
        log_write("failed to mount romfs 0x0100000000001000\n");
    }

    g_is_init = true;
    R_SUCCEED();
}

void Exit() {
    if (!g_is_init) {
        return;
    }

    SCOPED_MUTEX(&g_mutex);

    for (auto& id : g_sound_ids) {
        plsrPlayerFree(id);
    }

    plsrPlayerExit();

    std::memset(g_sound_ids, 0, sizeof(g_sound_ids));
    g_is_init = false;
}

Result PlaySoundEffect(SoundEffect effect) {
    R_UNLESS(g_is_init, 0x1);

    SCOPED_MUTEX(&g_mutex);
    const auto id = g_sound_ids[std::to_underlying(effect)];

    if (plsrPlayerIsPlaying(id)) {
        R_TRY(plsrPlayerStop(id));
        plsrPlayerWaitNextFrame(); // is this needed?
    }

    return plsrPlayerPlay(id);
}

} // namespace sphaira::audio
