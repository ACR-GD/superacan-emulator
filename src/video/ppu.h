#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <cstring>
#include "state_io.h"

class PPU {
public:
    static constexpr int SCREEN_WIDTH  = 320;
    static constexpr int SCREEN_HEIGHT = 240;
    static constexpr int TOTAL_LINES   = 262;

    PPU();

    void reset();

    // Register file: 0xF00000 - 0xF001FF (256 16-bit registers)
    uint16_t read_reg(uint32_t addr) const;
    void write_reg(uint32_t addr, uint16_t val);

    // Palette RAM: 0xF00200 - 0xF003FF (256 16-bit colors)
    uint16_t read_palette(uint32_t addr) const;
    void write_palette(uint32_t addr, uint16_t val);

    // VRAM: 0xF40000 - 0xF5FFFF (128 KB, 64K 16-bit words)
    uint16_t read_vram(uint32_t addr) const;
    void write_vram(uint32_t addr, uint16_t val);

    uint8_t read_vram_byte(uint32_t addr) const;
    void write_vram_byte(uint32_t addr, uint8_t val);

    // Direct access for DMA
    uint16_t* get_vram_ptr() { return vram.data(); }
    uint16_t* get_palette_ptr() { palette_dirty = true; return palette.data(); }

    void set_vpos(int line);
    int get_vpos() const { return vpos; }

    bool is_in_vblank() const { return in_vblank; }
    void set_vblank(bool v) { in_vblank = v; }

    // Render one scanline into framebuffer
    void render_scanline(int line);

    // The framebuffer is always SCREEN_WIDTH pixels per row. $F00008 bit 8 selects the 320 px
    // mode (set) or the 256 px mode (clear, e.g. The Son of Evil); in 256 px mode only the
    // first 256 columns are active. Frontends should show `get_active_width()` columns,
    // stretched to the same 4:3 picture (an H256 dot is 1.25 times wider on the hardware).
    const uint32_t* get_framebuffer() const { return framebuffer.data(); }
    int get_active_width() const { return active_width; }

    void serialize(StateIO& s) {
        s.pod(regs); s.pod(palette); s.pod(vram); s.pod(framebuffer);
        s.pod(vpos); s.pod(in_vblank);
        if (s.loading) {
            palette_dirty = true; vram_dirty = true;
            active_width = (regs[0x04] & 0x0100) ? SCREEN_WIDTH : 256;
        }
    }

private:
    std::array<uint16_t, 256> regs;
    std::array<uint16_t, 256> palette;
    std::array<uint16_t, 0x10000> vram; // 64K words = 128KB
    std::array<uint32_t, SCREEN_WIDTH * SCREEN_HEIGHT> framebuffer;

    int vpos;
    bool in_vblank;
    int active_width = SCREEN_WIDTH;   // width mode of the current frame, latched at line 0

    uint32_t color_to_argb(uint16_t c) const;

    void render_tilemap_line(int layer, int line, uint8_t* line_buf, uint8_t* prio_buf);
    void render_roz_line(int line, uint8_t* line_buf, uint8_t* prio_buf);
    void render_sprites_line(int line, uint8_t* line_buf, uint8_t* prio_buf);

    // Performance optimization: dirty flags
    bool palette_dirty = true;
    bool vram_dirty = true;
    std::array<uint32_t, 256> palette_cache;
    // Per-pixel "tie" bit of the frontmost background/ROZ pixel on the current line. With
    // tile-mode bit 9 set, map entries with bit 15 clear set it, and such a pixel covers an
    // equal-priority sprite (MiSTer um6618_mixer). Son of Evil hides characters behind walls
    // and roofs this way.
    std::array<uint8_t, SCREEN_WIDTH> tie_buf;
};
