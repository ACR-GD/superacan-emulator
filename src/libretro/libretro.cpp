// libretro core: runs the emulator inside RetroArch (Windows, Linux, macOS, Android, ...).
#include "libretro.h"
#include "emu.h"
#include "state_io.h"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "m68k.h"
}

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static SuperAcan* emu = nullptr;
static constexpr double SAMPLE_RATE = 44744.0;   // the APU's output rate (as the SDL build)

static void log(retro_log_level lvl, const char* fmt, ...) {
    if (!log_cb) return;
    char buf[512];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    log_cb(lvl, "%s", buf);
}

static bool exists(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

static long file_size(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f);
    return n;
}

// Super Dragon Force ships as two chips: the program ROM and a separate data ROM ("08002"),
// looked for next to the program ROM, as the other frontends do.
static std::string find_data_rom(const std::string& prg) {
    size_t slash = prg.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? "" : prg.substr(0, slash + 1);
    std::string name = prg.substr(dir.size());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    bool sdf = name.find("16007") != std::string::npos || name.find("dragon") != std::string::npos;
    if (!sdf || file_size(prg) > 0x200000) return "";   // >2 MB files carry their own data ROM
    std::vector<std::string> cands;
    if (name.find("_en") != std::string::npos) cands.push_back(dir + "Super-Dragon-Force_DAT_EN.bin");
    cands.push_back(dir + "08002 (Taiwan).bin");
    cands.push_back(dir + "08002.bin");
    for (auto& c : cands) if (exists(c)) return c;
    return "";
}

// Save state = 68000 context + bus (RAM, PPU, APU, ...) + sound-cycle carry, as SaveState.
static void serialize(StateIO& io) {
    std::vector<uint8_t> cpu(m68k_context_size());
    if (!io.loading) m68k_get_context(cpu.data());
    io.bytes(cpu.data(), cpu.size());
    emu->get_bus().serialize(io);
    io.pod(emu->sound_cycle_remainder());
    if (io.loading && io.ok) {
        m68k_set_context(cpu.data());
        emu->restore_cpu_hooks();
    }
}

extern "C" {

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_set_environment(retro_environment_t cb) {
    env_cb = cb;
    bool no_game = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    retro_log_callback logging;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) log_cb = logging.log;

    static const retro_controller_description pads[] = {{"Super A'Can Joypad", RETRO_DEVICE_JOYPAD}};
    static const retro_controller_info ports[] = {{pads, 1}, {pads, 1}, {nullptr, 0}};
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

    static const retro_variable vars[] = {
        {"superacan_sound", "Sound style; accurate|sustained"},
        {nullptr, nullptr},
    };
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

RETRO_API void retro_init(void) {}
RETRO_API void retro_deinit(void) {}

RETRO_API void retro_get_system_info(retro_system_info* info) {
    std::memset(info, 0, sizeof *info);
    info->library_name = "Super A'Can";
#ifndef CORE_VERSION
#define CORE_VERSION "3.1"
#endif
    info->library_version = CORE_VERSION;
    info->valid_extensions = "bin|rom";
    info->need_fullpath = true;     // the loader reads the file (byte order, 3 MB split, data ROM)
    info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(retro_system_av_info* info) {
    info->geometry.base_width = PPU::SCREEN_WIDTH;
    info->geometry.base_height = PPU::SCREEN_HEIGHT;
    info->geometry.max_width = PPU::SCREEN_WIDTH;
    info->geometry.max_height = PPU::SCREEN_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;     // 320 and 256 px modes both fill a 4:3 TV
    info->timing.fps = 60.0;
    info->timing.sample_rate = SAMPLE_RATE;
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}

static void apply_options() {
    retro_variable v{"superacan_sound", nullptr};
    if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value)
        emu->get_apu().set_sustained(std::strcmp(v.value, "sustained") == 0);
}

RETRO_API bool retro_load_game(const retro_game_info* game) {
    if (!game || !game->path) return false;
    retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;   // the PPU writes 0xFFRRGGBB
    if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        log(RETRO_LOG_ERROR, "[superacan] XRGB8888 not supported by the frontend\n");
        return false;
    }

    static const retro_input_descriptor desc[] = {
#define PAD(port) \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select"}, \
        {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},
        PAD(0) PAD(1)
#undef PAD
        {0, 0, 0, 0, nullptr},
    };
    env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)desc);

    emu = new SuperAcan();
    // Optional console ROMs in <system>/superacan/ (MAME's supracan set); games run without them.
    const char* sys = nullptr;
    if (env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys) && sys) {
        std::string bios = std::string(sys) + "/superacan";
        if (emu->load_bios(bios)) log(RETRO_LOG_INFO, "[superacan] console ROMs loaded from %s\n", bios.c_str());
    }
    std::string prg = game->path, dat = find_data_rom(prg);
    if (!dat.empty()) log(RETRO_LOG_INFO, "[superacan] data ROM: %s\n", dat.c_str());
    if (!emu->load_roms(prg, dat)) {
        log(RETRO_LOG_ERROR, "[superacan] cannot load %s\n", prg.c_str());
        delete emu; emu = nullptr;
        return false;
    }
    apply_options();
    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const retro_game_info*, size_t) { return false; }

RETRO_API void retro_unload_game(void) { delete emu; emu = nullptr; }

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

RETRO_API void retro_reset(void) { if (emu) emu->reset(); }

RETRO_API void retro_run(void) {
    bool updated = false;
    if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) apply_options();

    input_poll_cb();
    static const struct { unsigned id; JoypadButton btn; } map[] = {
        {RETRO_DEVICE_ID_JOYPAD_UP, BTN_UP},       {RETRO_DEVICE_ID_JOYPAD_DOWN, BTN_DOWN},
        {RETRO_DEVICE_ID_JOYPAD_LEFT, BTN_LEFT},   {RETRO_DEVICE_ID_JOYPAD_RIGHT, BTN_RIGHT},
        {RETRO_DEVICE_ID_JOYPAD_A, BTN_A},         {RETRO_DEVICE_ID_JOYPAD_B, BTN_B},
        {RETRO_DEVICE_ID_JOYPAD_X, BTN_X},         {RETRO_DEVICE_ID_JOYPAD_Y, BTN_Y},
        {RETRO_DEVICE_ID_JOYPAD_L, BTN_L},         {RETRO_DEVICE_ID_JOYPAD_R, BTN_R},
        {RETRO_DEVICE_ID_JOYPAD_SELECT, BTN_SELECT}, {RETRO_DEVICE_ID_JOYPAD_START, BTN_START},
    };
    for (int port = 0; port < 2; port++)
        for (auto& m : map)
            emu->get_joypad().set_button(port, m.btn, input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, m.id) != 0);

    emu->run_frame();

    video_cb(emu->get_framebuffer(), emu->get_active_width(), PPU::SCREEN_HEIGHT,
             PPU::SCREEN_WIDTH * sizeof(uint32_t));

    const auto& ab = emu->get_apu().get_audio_buffer();   // interleaved stereo int16
    size_t frames = ab.size() / 2, done = 0;
    while (done < frames) done += audio_batch_cb(ab.data() + done * 2, frames - done);
    emu->get_apu().clear_audio_buffer();
}

RETRO_API size_t retro_serialize_size(void) {
    if (!emu) return 0;
    StateIO io;
    serialize(io);
    return io.buf.size();
}

RETRO_API bool retro_serialize(void* data, size_t size) {
    if (!emu) return false;
    StateIO io;
    serialize(io);
    if (io.buf.size() > size) return false;
    std::memcpy(data, io.buf.data(), io.buf.size());
    return true;
}

RETRO_API bool retro_unserialize(const void* data, size_t size) {
    if (!emu) return false;
    StateIO probe;
    serialize(probe);
    if (size < probe.buf.size()) return false;
    StateIO io;
    io.loading = true;
    io.buf.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + probe.buf.size());
    serialize(io);
    return io.ok;
}

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char*) {}

// Battery save RAM (32 KB at $EC0000): RetroArch keeps it in a .srm file.
RETRO_API void* retro_get_memory_data(unsigned id) {
    if (emu && id == RETRO_MEMORY_SAVE_RAM) return emu->get_bus().get_sram().data();
    return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned id) {
    if (emu && id == RETRO_MEMORY_SAVE_RAM) return emu->get_bus().get_sram().size();
    return 0;
}

} // extern "C"
