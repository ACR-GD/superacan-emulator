// Footage recorder: record <prg> <dat> <out-prefix> [--load state] --frames N [hold:frame:len:btn ...]
// Writes <out>.rgb (320x240 RGB24, 60 fps) and <out>.s16 (stereo s16le, 44744 Hz) for ffmpeg.
#include "emu.h"
#include "savestate.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
int main(int argc, char** argv) {
    SuperAcan e; if (!e.load_roms(argv[1], argv[2])) return 1;
    std::string out = argv[3]; int frames = 0;
    struct H { int f, len; JoypadButton b; }; std::vector<H> holds;
    std::map<std::string, JoypadButton> names = {{"up",BTN_UP},{"down",BTN_DOWN},{"left",BTN_LEFT},{"right",BTN_RIGHT},
        {"a",BTN_A},{"b",BTN_B},{"x",BTN_X},{"y",BTN_Y},{"start",BTN_START},{"select",BTN_SELECT},{"l",BTN_L},{"r",BTN_R}};
    for (int i = 4; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--load") { if (!SaveState::load(e, argv[++i])) return 1; }
        else if (a == "--frames") frames = atoi(argv[++i]);
        else if (a.rfind("hold:", 0) == 0) { int f, l; char b[16]; sscanf(a.c_str(), "hold:%d:%d:%15s", &f, &l, b); holds.push_back({f, l, names.at(b)}); }
    }
    FILE* v = fopen((out + ".rgb").c_str(), "wb"); FILE* s = fopen((out + ".s16").c_str(), "wb");
    std::vector<uint8_t> rgb(320 * 240 * 3);
    for (int f = 0; f < frames; f++) {
        for (auto& kv : names) { bool on = false; for (auto& h : holds) if (h.b == kv.second && f >= h.f && f < h.f + h.len) on = true; e.get_joypad().set_button(0, kv.second, on); }
        e.get_apu().clear_audio_buffer(); e.run_frame();
        const uint32_t* fb = e.get_framebuffer();
        for (int i = 0; i < 320 * 240; i++) { rgb[i*3] = fb[i] >> 16; rgb[i*3+1] = fb[i] >> 8; rgb[i*3+2] = fb[i]; }
        fwrite(rgb.data(), 1, rgb.size(), v);
        const auto& a = e.get_apu().get_audio_buffer(); fwrite(a.data(), 2, a.size(), s);
    }
    fclose(v); fclose(s);
    return 0;
}
