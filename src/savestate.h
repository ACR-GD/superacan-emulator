#pragma once
#include <string>
#include <cstdint>

class SuperAcan;

// Save states (format v2): full machine snapshot — 68000 context, work RAM, PPU, APU (6502 +
// sampler), joypad, system registers and DMA. States are tagged with a fingerprint of the
// program ROM so a state can never be loaded into a different game.
class SaveState {
public:
    static constexpr uint32_t MAGIC = 0x53414341; // "SACA"
    static constexpr uint32_t VERSION = 2;

    static bool save(const SuperAcan& emu, const std::string& path);
    static bool load(SuperAcan& emu, const std::string& path);

    static std::string get_quick_save_path(int slot);
};
