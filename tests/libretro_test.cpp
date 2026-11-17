// Minimal libretro frontend to check the core without RetroArch:
//   libretro_test CORE ROM FRAMES OUT.ppm
// Runs FRAMES frames (Start held around frame 300), writes the last frame, counts audio, and
// checks that a save state taken halfway replays to the same final frame.
#include "libretro.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define SYM(name) auto name = (decltype(&::name))dlsym(h, #name); if (!name) { printf("missing %s\n", #name); return 1; }

static std::vector<uint32_t> frame; static unsigned fw, fh; static size_t audio_frames; static int cur;
static bool env(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(retro_pixel_format*)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data = "."; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: ((retro_variable*)data)->value = nullptr; return false;
    default: return false;
    }
}
static void video(const void* d, unsigned w, unsigned hgt, size_t pitch) {
    if (!d) return;
    fw = w; fh = hgt; frame.resize(w * hgt);
    for (unsigned y = 0; y < hgt; y++) memcpy(&frame[y * w], (const char*)d + y * pitch, w * 4);
}
static size_t audio(const int16_t*, size_t n) { audio_frames += n; return n; }
static void poll() {}
static int16_t input(unsigned port, unsigned, unsigned, unsigned id) {
    return port == 0 && id == RETRO_DEVICE_ID_JOYPAD_START && cur % 200 > 190;
}
static uint64_t hash() { uint64_t x = 1469598103934665603ULL; for (uint32_t p : frame) { x ^= p; x *= 1099511628211ULL; } return x; }

int main(int argc, char** argv) {
    if (argc < 5) { printf("usage: %s CORE ROM FRAMES OUT.ppm\n", argv[0]); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW);
    if (!h) { printf("%s\n", dlerror()); return 1; }
    SYM(retro_set_environment) SYM(retro_set_video_refresh) SYM(retro_set_audio_sample_batch)
    SYM(retro_set_input_poll) SYM(retro_set_input_state) SYM(retro_init) SYM(retro_load_game)
    SYM(retro_run) SYM(retro_serialize_size) SYM(retro_serialize) SYM(retro_unserialize)
    SYM(retro_get_system_av_info) SYM(retro_get_memory_size) SYM(retro_unload_game) SYM(retro_deinit)
    retro_set_environment(env); retro_set_video_refresh(video); retro_set_audio_sample_batch(audio);
    retro_set_input_poll(poll); retro_set_input_state(input); retro_init();
    retro_game_info gi{argv[2], nullptr, 0, nullptr};
    if (!retro_load_game(&gi)) { printf("load failed\n"); return 1; }
    retro_system_av_info av; retro_get_system_av_info(&av);
    int n = atoi(argv[3]);
    std::vector<uint8_t> st;
    for (cur = 0; cur < n; cur++) {
        if (cur == n / 2) { st.resize(retro_serialize_size()); if (!retro_serialize(st.data(), st.size())) return 1; }
        retro_run();
    }
    uint64_t a = hash();
    if (!retro_unserialize(st.data(), st.size())) { printf("unserialize failed\n"); return 1; }
    for (cur = n / 2; cur < n; cur++) retro_run();
    uint64_t b = hash();
    printf("%ux%u fps %.2f rate %.0f | audio %.1f frames/video frame | state %zu bytes | sram %zu | replay %s\n",
           fw, fh, av.timing.fps, av.timing.sample_rate, audio_frames / (double)(n + n - n / 2), st.size(),
           retro_get_memory_size(RETRO_MEMORY_SAVE_RAM), a == b ? "identical" : "DIFFERENT");
    FILE* f = fopen(argv[4], "wb"); fprintf(f, "P6 %u %u 255\n", fw, fh);
    for (uint32_t p : frame) { fputc(p >> 16, f); fputc(p >> 8, f); fputc(p, f); }
    fclose(f);
    retro_unload_game(); retro_deinit();
    return a == b ? 0 : 1;
}
