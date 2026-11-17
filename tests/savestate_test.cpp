// Determinism test: state saved at frame N must reproduce identical video+audio afterwards.
#include "emu.h"
#include "savestate.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

static uint64_t hash_run(SuperAcan& emu, int frames, int press_at) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    };
    for (int f = 0; f < frames; f++) {
        emu.get_joypad().set_button(0, BTN_START, f == press_at || f == press_at + 1);
        emu.get_apu().clear_audio_buffer();
        emu.run_frame();
        mix(emu.get_framebuffer(), 320 * 240 * 4);
        const auto& a = emu.get_apu().get_audio_buffer();
        mix(a.data(), a.size() * 2);
    }
    return h;
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s prg dat mode(save|load|both) [state]\n", argv[0]); return 2; }
    std::string mode = argv[3], state = argc > 4 ? argv[4] : "/tmp/sa_test.state";
    SuperAcan emu;
    if (!emu.load_roms(argv[1], argv[2])) return 2;
    if (mode == "save" || mode == "both") {
        for (int i = 0; i < 2600; i++) {                         // reach the title / menus
            emu.get_joypad().set_button(0, BTN_START, i == 2400 || i == 2401);
            emu.get_apu().clear_audio_buffer();
            emu.run_frame();
        }
        if (!SaveState::save(emu, state)) return 1;
        uint64_t a = hash_run(emu, 600, 100);
        printf("after-save hash  %016llx\n", (unsigned long long)a);
        FILE* f = fopen((state + ".hash").c_str(), "w"); fprintf(f, "%016llx", (unsigned long long)a); fclose(f);
        if (mode == "save") return 0;
    }
    // load (in the same process for "both", or a fresh process for "load")
    if (!SaveState::load(emu, state)) return 1;
    uint64_t b = hash_run(emu, 600, 100);
    char expect[32] = {0};
    FILE* f = fopen((state + ".hash").c_str(), "r"); fread(expect, 1, 16, f); fclose(f);
    char got[32]; snprintf(got, sizeof got, "%016llx", (unsigned long long)b);
    printf("after-load hash  %s  -> %s\n", got, strcmp(got, expect) == 0 ? "IDENTICAL ✅" : "DIFFERENT ❌");
    return strcmp(got, expect) == 0 ? 0 : 1;
}
