// Scripted play harness: play <prg> <dat> [--load s] [--save s] --frames N [hold:frame:len:btn ...] [shot:frame:out.ppm ...]

// Scripted play harness: play [--load s] [--save s] --frames N [hold:frame:len:btn ...] [shot:frame:path ...]
#include "emu.h"
#include "savestate.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>

static JoypadButton btn(const std::string& n) {
    static std::map<std::string, JoypadButton> m = {{"up",BTN_UP},{"down",BTN_DOWN},{"left",BTN_LEFT},{"right",BTN_RIGHT},
        {"a",BTN_A},{"b",BTN_B},{"x",BTN_X},{"y",BTN_Y},{"start",BTN_START},{"select",BTN_SELECT},{"l",BTN_L},{"r",BTN_R}};
    return m.at(n);
}
static void shot(SuperAcan& e, const char* path) {
    FILE* f = fopen(path, "wb"); const uint32_t* fb = e.get_framebuffer();
    fprintf(f, "P6\n320 240\n255\n");
    for (int i = 0; i < 320 * 240; i++) { uint8_t p[3] = {(uint8_t)(fb[i] >> 16), (uint8_t)(fb[i] >> 8), (uint8_t)fb[i]}; fwrite(p, 1, 3, f); }
    fclose(f);
}
int main(int argc, char** argv) {
    SuperAcan e; std::string load, save; int frames = 0;
    struct Hold { int f, len; JoypadButton b; }; std::vector<Hold> holds;
    struct Shot { int f; std::string p; }; std::vector<Shot> shots;
    if (!e.load_roms(argv[1], argv[2])) return 1;
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--load") load = argv[++i]; else if (a == "--save") save = argv[++i];
        else if (a == "--frames") frames = atoi(argv[++i]);
        else if (a.rfind("hold:", 0) == 0) { int f, l; char b[16]; sscanf(a.c_str(), "hold:%d:%d:%15s", &f, &l, b); holds.push_back({f, l, btn(b)}); }
        else if (a.rfind("shot:", 0) == 0) { int f; char p[512]; sscanf(a.c_str(), "shot:%d:%511s", &f, p); shots.push_back({f, p}); }
    }
    if (!load.empty() && !SaveState::load(e, load)) return 1;
    for (int f = 0; f < frames; f++) {
        for (auto b : {BTN_UP,BTN_DOWN,BTN_LEFT,BTN_RIGHT,BTN_A,BTN_B,BTN_X,BTN_Y,BTN_START,BTN_SELECT,BTN_L,BTN_R}) {
            bool on = false; for (auto& h : holds) if (h.b == b && f >= h.f && f < h.f + h.len) on = true;
            e.get_joypad().set_button(0, b, on);
        }
        e.get_apu().clear_audio_buffer(); e.run_frame();
        for (auto& s : shots) if (s.f == f) shot(e, s.p.c_str());
    }
    if (!save.empty()) SaveState::save(e, save);
}
