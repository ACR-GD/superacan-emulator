#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include "video/ppu.h"
#include "input/joypad.h"
#include "sound/apu.h"

class Bus {
public:
    Bus();

    void reset();

    bool load_prg_rom(const std::string& path);
    bool load_dat_rom(const std::string& path);

    // Optional console boot ROM (internal_68k.bin, 4 KB) and lockout-chip key (umc6650.bin,
    // 16 bytes). With them the console boots through its own BIOS like the hardware/MAME;
    // without them the bus starts the cartridge directly ("direct boot").
    void set_boot_rom(const std::vector<uint8_t>& rom68k, const std::vector<uint8_t>& key6650);
    bool has_boot_rom() const { return !boot68k.empty(); }

    // Direct buffer loading
    bool load_prg_buffer(const uint8_t* data, size_t size);
    bool load_dat_buffer(const uint8_t* data, size_t size);

    // 68000 memory access
    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    uint32_t read32(uint32_t addr);

    void write8(uint32_t addr, uint8_t val);
    void write16(uint32_t addr, uint16_t val);
    void write32(uint32_t addr, uint32_t val);

    PPU& get_ppu() { return ppu; }
    const PPU& get_ppu() const { return ppu; }
    Joypad& get_joypad() { return joypad; }
    const Joypad& get_joypad() const { return joypad; }
    APU& get_apu() { return apu; }
    const APU& get_apu() const { return apu; }

    // Direct memory access (for save states and debugging)
    const uint8_t* get_wram() const { return wram.data(); }
    uint8_t* get_wram() { return wram.data(); }
    const std::vector<uint8_t>& get_prg_rom() const { return prg_rom; }
    const std::vector<uint8_t>& get_dat_rom() const { return dat_rom; }

    uint16_t get_irq_mask() const { return irq_mask; }

    // Free running counter ($E90014-$E90018), the source of interrupt level 3.
    // Advance by 68000 cycles (and once per frame for the frame-counting mode).
    void step_frc(int cpu_cycles);
    void frc_vblank();
    bool frc_irq_pending() const { return frc_irq; }
    void ack_frc_irq() { frc_irq = false; }

    // Battery save RAM (in-game saves). The frontend loads/stores it as a .sav file.
    std::array<uint8_t, 0x8000>& get_sram() { return sram; }
    bool take_sram_dirty() { bool d = sram_dirty; sram_dirty = false; return d; }

    void serialize(StateIO& s) {
        s.pod(wram);
        ppu.serialize(s);
        joypad.serialize(s);
        apu.serialize(s);
        s.pod(irq_mask); s.pod(frc_ctrl); s.pod(frc_reload); s.pod(frc_value);
        s.pod(overlay_reg); s.pod(egg_reg); s.pod(dma);
        s.pod(sram);   // last, so states from before SRAM support can still be loaded
    }

    // Direct boot vector fetch
    uint32_t get_initial_ssp() const;
    uint32_t get_initial_pc() const;

private:
    std::vector<uint8_t> prg_rom;  // 2 MB
    std::vector<uint8_t> dat_rom;  // 1 MB
    std::vector<uint8_t> boot68k;  // console boot ROM (68k order), mapped until locked out
    std::array<uint8_t, 16> key6650{};
    std::array<uint8_t, 32> ram6650{};
    uint8_t addr6650 = 0x7F;
    bool lo_locked = true, hi_locked = true;   // boot ROM hidden at $000000 / $F80000
    uint8_t read6650(uint32_t addr);
    void write6650(uint32_t addr, uint8_t val);
    bool prg_byteswapped = false;  // DAT ROMs use the same byte order as their PRG ROM
    std::array<uint8_t, 0x10000> wram;     // 64 KB Work RAM (mirrored at FC0000-FFFFFF)

    // Battery-backed cartridge SRAM (62256, 32 KB) at $EC0000-$ECFFFF, wired to the low byte
    // lane only (odd addresses), so the 64 KB window holds 32 KB. Blank SRAM reads as 0xFF.
    std::array<uint8_t, 0x8000> sram;
    bool sram_dirty = false;
    static bool is_sram(uint32_t addr) { return (addr & 0xFF0000) == 0xEC0000; }
    static uint32_t sram_index(uint32_t addr) { return ((addr - 0xEC0000) >> 1) & 0x7FFF; }

    PPU ppu;
    Joypad joypad;
    APU apu;

    // System / Host registers (E90000-E9003F)
    uint16_t irq_mask;       // E90010
    uint16_t frc_ctrl;       // E90014
    uint16_t frc_reload;     // E90016
    uint16_t frc_value;      // E90018
    uint32_t frc_sub = 0;    // CPU cycles inside the current counting unit
    bool     frc_irq = false;
    uint16_t overlay_reg;    // E9001C
    uint16_t egg_reg;        // E90B3C

    // Host DMA channels (E90020-E9003F)
    struct DMAChannel {
        uint32_t src;
        uint32_t dst;
        uint16_t count;
        uint16_t ctrl;
    } dma[2];

    void execute_host_dma(int ch);
    void execute_video_dma();
};

extern Bus* g_bus;
