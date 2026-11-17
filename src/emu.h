#pragma once
#include <string>
#include <cstdint>
#include "bus/bus.h"

class SuperAcan {
public:
    SuperAcan();
    ~SuperAcan();

    bool load_roms(const std::string& prg_path, const std::string& dat_path = "");

    // Optional console system ROMs (MAME's supracan set). `dir` must contain
    // internal_6502_1.bin and internal_6502_2.bin. Call before load_roms(). Returns false if
    // they're missing or the wrong size; the emulator then runs without them.
    bool load_bios(const std::string& dir);
    void reset();

    void run_frame();

    const uint32_t* get_framebuffer() const {
        return bus.get_ppu().get_framebuffer();
    }
    // Active columns of the framebuffer this frame: 320, or 256 in the H256 mode. Rows are
    // always PPU::SCREEN_WIDTH pixels apart; show the active part stretched to 4:3.
    int get_active_width() const {
        return bus.get_ppu().get_active_width();
    }

    // Re-installs Musashi's callback/cycle-table pointers after m68k_set_context().
    void restore_cpu_hooks();

    uint32_t get_pc() const;
    uint32_t get_sp() const;

    Joypad& get_joypad() {
        return bus.get_joypad();
    }

    APU& get_apu() {
        return bus.get_apu();
    }
    const APU& get_apu() const {
        return bus.get_apu();
    }

    Bus& get_bus() {
        return bus;
    }
    const Bus& get_bus() const {
        return bus;
    }

private:
    Bus bus;
    bool running;
    int sound_cycle_rem = 0; // fractional 68k->6502 cycle carry (part of save states)

public:
    int& sound_cycle_remainder() { return sound_cycle_rem; }
};
