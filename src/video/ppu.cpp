#include "ppu.h"
#include <algorithm>

PPU::PPU() {
    reset();
}

void PPU::reset() {
    regs.fill(0);
    palette.fill(0);
    palette_dirty = true;
    vram.fill(0);
    framebuffer.fill(0xFF000000); // Black screen
    vpos = 0;
    in_vblank = false;
    active_width = SCREEN_WIDTH;

    // Default register values per MiSTer
    regs[0x04] = 0x0100; // H320 default (bit 8)
}

uint16_t PPU::read_reg(uint32_t addr) const {
    uint32_t index = (addr >> 1) & 0xFF;
    if (index == 0x00) {
        return (in_vblank ? 0x8000 : 0x0000);
    } else if (index == 0x01) {
        return static_cast<uint16_t>(vpos);
    } else if (index == 0x02) {
        return 0; // hpos
    }
    return regs[index];
}

void PPU::write_reg(uint32_t addr, uint16_t val) {
    uint32_t index = (addr >> 1) & 0xFF;
    regs[index] = val;
}

uint16_t PPU::read_palette(uint32_t addr) const {
    uint32_t index = (addr >> 1) & 0xFF;
    return palette[index];
}

void PPU::write_palette(uint32_t addr, uint16_t val) {
    uint32_t index = (addr >> 1) & 0xFF;
    if (palette[index] != val) {
        palette[index] = val;
        palette_dirty = true;
    }
}

uint16_t PPU::read_vram(uint32_t addr) const {
    uint32_t word_addr = (addr >> 1) & 0xFFFF;
    return vram[word_addr];
}

void PPU::write_vram(uint32_t addr, uint16_t val) {
    uint32_t word_addr = (addr >> 1) & 0xFFFF;
    if (vram[word_addr] != val) {
        vram[word_addr] = val;
        vram_dirty = true;
    }
}

uint8_t PPU::read_vram_byte(uint32_t addr) const {
    uint32_t word_addr = (addr >> 1) & 0xFFFF;
    uint16_t w = vram[word_addr];
    return (addr & 1) ? static_cast<uint8_t>(w & 0xFF) : static_cast<uint8_t>(w >> 8);
}

void PPU::write_vram_byte(uint32_t addr, uint8_t val) {
    uint32_t word_addr = (addr >> 1) & 0xFFFF;
    uint16_t w = vram[word_addr];
    if (addr & 1) {
        w = (w & 0xFF00) | val;
    } else {
        w = (w & 0x00FF) | (static_cast<uint16_t>(val) << 8);
    }
    vram[word_addr] = w;
}

void PPU::set_vpos(int line) {
    vpos = line;
    regs[0x01] = static_cast<uint16_t>(line);
}

uint32_t PPU::color_to_argb(uint16_t c) const {
    // Super A'Can UM70C188 format: xBGR555
    uint32_t r = c & 0x1F;
    uint32_t g = (c >> 5) & 0x1F;
    uint32_t b = (c >> 10) & 0x1F;

    uint32_t r8 = (r << 3) | (r >> 2);
    uint32_t g8 = (g << 3) | (g >> 2);
    uint32_t b8 = (b << 3) | (b >> 2);

    return 0xFF000000 | (r8 << 16) | (g8 << 8) | b8;
}

static inline int ysize_of(int code) {
    switch (code) {
        case 0:  return 1;
        case 1:  return 2;
        case 2:  return 3;
        case 3:  return 4;
        case 4:  return 5;
        case 5:  return 6;
        case 6:  return 7;
        case 7:  return 8;
        case 8:  return 9;
        case 9:  return 10;
        case 10: return 12;
        case 11: return 16;
        case 12: return 20;
        case 13: return 22;
        case 14: return 24;
        default: return 26;
    }
}

void PPU::render_tilemap_line(int layer, int line, uint8_t* line_buf, uint8_t* prio_buf) {
    // Layer registers start at 0x80 + layer * 0x10
    uint32_t base = 0x80 + layer * 0x10;
    uint16_t flags     = regs[base + 0];
    uint16_t tile_mode = regs[base + 1];
    uint16_t scr_x     = regs[base + 2];
    uint16_t scr_y     = regs[base + 3];
    uint16_t map_base  = regs[base + 4];
    uint16_t mode      = regs[base + 5];
    uint16_t lsx_base  = regs[base + 6];
    uint16_t lsel_base = regs[base + 7];

    // Base addresses in VRAM are shifted left by 1 ({base[14:0], 1'b0})
    uint32_t map_base_words = (static_cast<uint32_t>(map_base) << 1) & 0xFFFF;
    uint32_t lsx_base_words = (static_cast<uint32_t>(lsx_base) << 1) & 0xFFFF;
    uint32_t lsel_base_words = (static_cast<uint32_t>(lsel_base) << 1) & 0xFFFF;

    uint8_t prio = (flags >> 13) & 7;
    bool wrap = (flags & (1 << 5)) != 0;
    bool pal_hi = (tile_mode & (1 << 9)) != 0;
    bool use_lsx = (tile_mode & (1 << 14)) != 0;
    bool use_sel = (tile_mode & (1 << 11)) != 0;
    bool per_line = (tile_mode & (1 << 8)) != 0;

    int xs_log = 5, ys_log = 5;
    switch ((flags >> 8) & 0xF) {
        case 0x2: xs_log = 4; ys_log = 4; break; // 16x16
        case 0x4: xs_log = 5; ys_log = 5; break; // 32x32
        case 0x6: xs_log = 6; ys_log = 5; break; // 64x32
        case 0xA: xs_log = 7; ys_log = 5; break; // 128x32
        case 0xC: xs_log = 6; ys_log = 6; break; // 64x64
        default:  xs_log = 5; ys_log = 5; break;
    }

    int tiles_w = 1 << xs_log;
    int tiles_h = 1 << ys_log;
    int x_mask = (tiles_w * 8) - 1;
    int y_mask = (tiles_h * 8) - 1;

    // Scroll values (11-bit X, 12-bit Y, signed)
    int scroll_x = scr_x & 0x7FF;
    if (scroll_x & 0x400) scroll_x -= 0x800;
    if (flags & (1 << 1)) scroll_x ^= x_mask;

    int scroll_y = scr_y & 0xFFF;
    if (scroll_y & 0x800) scroll_y -= 0x1000;
    if (flags & (1 << 0)) scroll_y ^= y_mask;

    // Line check for non-wrapped layers
    if (!wrap && (line + scroll_y < 0 || line + scroll_y > y_mask)) {
        return;
    }

    // Line selection and line scrolling
    int real_y;
    if (use_sel) {
        int16_t y_sel = static_cast<int16_t>(vram[(lsel_base_words + line) & 0xFFFF]);
        real_y = (y_sel + scroll_y) & y_mask;
    } else {
        real_y = (line + scroll_y) & y_mask;
    }

    if (use_lsx) {
        uint16_t x_sel = vram[(lsx_base_words + (per_line ? line : 0)) & 0xFFFF];
        scroll_x += static_cast<int16_t>(x_sel);
    }

    // Determine region (color depth): 0 = 8bpp, 1 = 4bpp, 2 = 2bpp
    int gfx_mode = regs[0xF8] & 7;
    int region = 2; // default 2bpp
    if (layer == 0) {
        switch (gfx_mode) {
            case 0: region = 2; break; // 2bpp
            case 1: region = 1; break; // 4bpp
            case 2: region = 0; break; // 8bpp
            case 3: region = 1; break; // 4bpp
            default: region = 0; break;
        }
    } else if (layer == 1) {
        switch (gfx_mode) {
            case 0: region = 2; break; // 2bpp
            case 1: region = 1; break; // 4bpp
            case 2: region = 1; break; // 4bpp
            case 3: region = 1; break; // 4bpp
            default: region = 2; break;
        }
    } else {
        region = 2; // Layers 2 and 3 are always 2bpp
    }

    uint32_t pat_base_offset = (static_cast<uint32_t>((mode >> 12) & 7)) << (8 + region);

    int tile_row = real_y >> 3;
    int py_base = real_y & 7;

    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        int real_x_full = x + scroll_x;
        if (!wrap && (real_x_full < 0 || real_x_full > x_mask)) {
            continue;
        }
        int real_x = real_x_full & x_mask;
        int tile_col = real_x >> 3;
        int px_base = real_x & 7;

        uint32_t map_idx = (tile_row << xs_log) | tile_col;
        uint16_t map_entry = vram[(map_base_words + map_idx) & 0xFFFF];

        uint32_t tile_no = (map_entry & 0x3FF) + pat_base_offset;
        bool flip_v = (map_entry & (1 << 10)) != 0;
        bool flip_h = (map_entry & (1 << 11)) != 0;
        uint8_t row_pal = static_cast<uint8_t>((map_entry >> 12) | (pal_hi ? 8 : 0));
        uint8_t tie = (pal_hi && !(map_entry & 0x8000)) ? 1 : 0;

        int py = flip_v ? (py_base ^ 7) : py_base;
        int px = flip_h ? (px_base ^ 7) : px_base;

        uint8_t pixel_val = 0;
        bool opaque = false;

        if (region == 0) {
            // 8bpp: 64 bytes/tile, 8 bytes/row
            uint32_t byte_addr = (tile_no * 64) + (py * 8) + px;
            uint8_t b = read_vram_byte(byte_addr);
            if (b != 0) {
                pixel_val = b;
                opaque = true;
            }
        } else if (region == 1) {
            // 4bpp: 32 bytes/tile, 4 bytes/row
            uint32_t byte_addr = (tile_no * 32) + (py * 4) + (px >> 1);
            uint8_t b = read_vram_byte(byte_addr);
            uint8_t nib = (px & 1) ? (b & 0xF) : (b >> 4);
            if (nib != 0) {
                pixel_val = (row_pal << 4) | nib;
                opaque = true;
            }
        } else {
            // 2bpp: 16 bytes/tile, 2 bytes/row
            uint32_t byte_addr = (tile_no * 16) + (py * 2) + (px >> 2);
            uint8_t b = read_vram_byte(byte_addr);
            uint8_t shift = (3 - (px & 3)) * 2;
            uint8_t bits = (b >> shift) & 3;
            if (bits != 0) {
                pixel_val = (row_pal << 4) | bits; // 2bpp uses 16-color palette stride
                opaque = true;
            }
        }

        // Layers are drawn 3..0, so the lower-numbered layer wins a tie, unless the one
        // already there has the tie bit and this one does not (um6618_mixer "raises T").
        if (opaque && (prio < prio_buf[x] || (prio == prio_buf[x] && !(tie_buf[x] && !tie)))) {
            line_buf[x] = pixel_val;
            prio_buf[x] = prio;
            tie_buf[x] = tie;
        }
    }
}

void PPU::render_sprites_line(int line, uint8_t* line_buf, uint8_t* prio_buf) {
    uint16_t spr_base_word = (regs[0x10] << 1) & 0xFFFF; // $F00020 {base[14:0], 1'b0}
    // $F00022 holds the index of the last sprite, so count = value + 1 (MiSTer: count_reg + 1,
    // capped at 128). Using the value directly dropped the last sprite of every frame.
    int spr_count          = (regs[0x11] & 0x7FFF) + 1; // $F00022
    uint16_t spr_flag      = regs[0x13]; // $F00026

    if (spr_count > 128) spr_count = 128;

    bool spr_region = ((spr_flag & 1) == 0); // 1 = 4bpp, 0 = 8bpp

    for (int i = 0; i < spr_count; ++i) {
        uint32_t entry_addr = (spr_base_word + i * 4) & 0xFFFF;
        uint16_t w0 = vram[entry_addr + 0];
        uint16_t w1 = vram[entry_addr + 1];
        uint16_t w2 = vram[entry_addr + 2];
        uint16_t w3 = vram[entry_addr + 3];

        if (w3 == 0) continue;

        int y = w0 & 0x1FF;
        if (y >= 0x180) y -= 0x200;

        int height_tiles = ysize_of((w0 >> 9) & 0xF);
        int height = height_tiles * 8;

        if (line < y || line >= y + height) {
            continue;
        }

        int width_tiles = 1 << (w1 & 7);
        int width = width_tiles * 8;

        bool flip_y = (w1 & (1 << 10)) != 0;
        bool flip_x = (w1 & (1 << 11)) != 0;
        uint8_t prio = (w2 >> 9) & 3;
        bool direct = (w3 & (1 << 15)) != 0 || (width_tiles == 1 && height_tiles == 1);

        int x = w2 & 0x1FF;
        if (x >= 0x180) x -= 0x200;

        int py = line - y;
        if (flip_y) {
            py = height - 1 - py;
        }
        int tile_row = py >> 3;
        int py_in_tile = py & 7;

        for (int sx = 0; sx < width; ++sx) {
            int scr_x = x + sx;
            if (scr_x < 0 || scr_x >= SCREEN_WIDTH) continue;

            int use_sx = flip_x ? (width - 1 - sx) : sx;
            int tile_col = use_sx >> 3;
            int px_in_tile = use_sx & 7;

            uint16_t entry = 0;
            if (direct) {
                entry = w3;
            } else {
                uint32_t map_offset = (tile_row * width_tiles) + tile_col;
                uint32_t map_table = (static_cast<uint32_t>(w3) << 1) & 0xFFFF;
                entry = vram[(map_table + map_offset) & 0xFFFF];
            }

            uint32_t bank = (w1 >> 12) & 0xF;
            uint32_t tile_no = (entry & 0x3FF) + (bank << (spr_region ? 9 : 8));
            bool tile_flip_v = (entry & (1 << 10)) != 0;
            bool tile_flip_h = (entry & (1 << 11)) != 0;
            uint8_t pal = static_cast<uint8_t>((entry >> 12) & 0xF);

            int final_py = tile_flip_v ? (py_in_tile ^ 7) : py_in_tile;
            int final_px = tile_flip_h ? (px_in_tile ^ 7) : px_in_tile;

            uint8_t pixel_val = 0;
            bool opaque = false;

            if (spr_region) {
                // 4bpp
                uint32_t byte_addr = (tile_no * 32) + (final_py * 4) + (final_px >> 1);
                uint8_t b = read_vram_byte(byte_addr);
                uint8_t nib = (final_px & 1) ? (b & 0xF) : (b >> 4);
                if (nib != 0) {
                    pixel_val = (pal << 4) | nib;
                    opaque = true;
                }
            } else {
                // 8bpp
                uint32_t byte_addr = (tile_no * 64) + (final_py * 8) + final_px;
                uint8_t b = read_vram_byte(byte_addr);
                if (b != 0) {
                    pixel_val = b;
                    opaque = true;
                }
            }

            // An equal-priority background pixel with the tie bit covers the sprite.
            if (opaque && (prio < prio_buf[scr_x] || (prio == prio_buf[scr_x] && !tie_buf[scr_x]))) {
                line_buf[scr_x] = pixel_val;
                prio_buf[scr_x] = prio;
                tie_buf[scr_x] = 0;
            }
        }
    }
}

void PPU::render_roz_line(int line, uint8_t* line_buf, uint8_t* prio_buf) {
    uint16_t mode      = regs[0xC0]; // 0x180 >> 1
    uint16_t tile_mode = regs[0xC1]; // 0x182 >> 1
    uint16_t sxh       = regs[0xC2]; // 0x184 >> 1
    uint16_t sxl       = regs[0xC3]; // 0x186 >> 1
    uint16_t syh       = regs[0xC4]; // 0x188 >> 1
    uint16_t syl       = regs[0xC5]; // 0x18A >> 1
    int16_t  coef_a    = static_cast<int16_t>(regs[0xC6]); // 0x18C >> 1
    int16_t  coef_b    = static_cast<int16_t>(regs[0xC7]); // 0x18E >> 1
    int16_t  coef_c    = static_cast<int16_t>(regs[0xC8]); // 0x190 >> 1
    int16_t  coef_d    = static_cast<int16_t>(regs[0xC9]); // 0x192 >> 1
    uint16_t map_base  = regs[0xCA]; // 0x194 >> 1
    uint16_t bank_reg  = regs[0xCB]; // 0x196 >> 1
    uint16_t tab0_base = regs[0xCC]; // 0x198 >> 1
    uint16_t tabx_base = regs[0xCD]; // 0x19A >> 1
    uint16_t tab3_base = regs[0xCE]; // 0x19C >> 1
    uint16_t taby_base = regs[0xCF]; // 0x19E >> 1

    uint32_t map_base_words = (static_cast<uint32_t>(map_base) << 1) & 0xFFFF;
    uint8_t prio = (mode >> 13) & 7;
    bool wrap = (mode & (1 << 5)) != 0;

    int xs_log = 5, ys_log = 5;
    switch ((mode >> 8) & 0xF) {
        case 0x2: xs_log = 4; ys_log = 4; break; // 16x16
        case 0x4: xs_log = 5; ys_log = 5; break; // 32x32
        case 0x6: xs_log = 6; ys_log = 5; break; // 64x32
        case 0xA: xs_log = 7; ys_log = 5; break; // 128x32
        case 0xC: xs_log = 6; ys_log = 6; break; // 64x64
        default:  xs_log = 5; ys_log = 5; break;
    }

    int x_limit = 1 << (xs_log + 3);
    int y_limit = 1 << (ys_log + 3);

    int region = 0;
    switch (mode & 3) {
        case 0: region = 4; break; // 1bpp boot logo
        case 1: region = 2; break; // 2bpp
        case 2: region = 1; break; // 4bpp
        default: region = 0; break; // 8bpp
    }

    uint16_t pixel_mode = regs[0xF8]; // 0x1F0 >> 1
    bool bitmap = (region == 0) && (((pixel_mode >> 3) & 3) == 1);

    bool use_lines = (tile_mode & (1 << 14)) != 0;
    bool use_y_lines = use_lines && ((tile_mode & (1 << 15)) != 0);
    bool x_per_line = ((tile_mode & (1 << 8)) != 0) || bitmap || ((mode & 3) == 0);

    // Sign extend 24-bit scroll values to 32-bit signed
    int32_t scroll_x_reg = (static_cast<int32_t>(static_cast<int8_t>(sxh & 0xFF)) << 16) | sxl;
    int32_t scroll_y_reg = (static_cast<int32_t>(static_cast<int8_t>(syh & 0xFF)) << 16) | syl;

    int32_t scroll_x_line = scroll_x_reg;
    int32_t scroll_y_line = scroll_y_reg;

    if (use_lines) {
        if (tile_mode & (1 << 6)) { // table 0 replaces coef_a
            uint32_t t0_addr = ((static_cast<uint32_t>(tab0_base) << 1) + line) & 0xFFFF;
            coef_a = static_cast<int16_t>(vram[t0_addr]);
            if (coef_a == 0) return; // A zero in tab0 turns the line off per MiSTer RTL
        }
        uint32_t tx_addr = ((static_cast<uint32_t>(tabx_base) << 1) + (x_per_line ? (line * 2) : 0)) & 0xFFFF;
        if (x_per_line) {
            uint16_t dx_hi = vram[tx_addr];
            uint16_t dx_lo = vram[(tx_addr + 1) & 0xFFFF];
            scroll_x_line += static_cast<int32_t>((static_cast<uint32_t>(dx_hi) << 16) | dx_lo);
        } else {
            int16_t dx = static_cast<int16_t>(vram[tx_addr]);
            scroll_x_line += static_cast<int32_t>(dx) << 8;
        }
        if (use_y_lines) {
            uint32_t ty_addr = ((static_cast<uint32_t>(taby_base) << 1) + (line * 2)) & 0xFFFF;
            uint16_t dy_hi = vram[ty_addr];
            uint16_t dy_lo = vram[(ty_addr + 1) & 0xFFFF];
            scroll_y_line += static_cast<int32_t>((static_cast<uint32_t>(dy_hi) << 16) | dy_lo);
        }
    }

    int32_t start_accx = scroll_x_line + (line * coef_b);
    int32_t start_accy = use_y_lines ? scroll_y_line : (scroll_y_line + (line * coef_d));

    uint32_t tile_bank = 0;
    if (region == 0) {
        tile_bank = ((bank_reg >> 12) & 0xF) << 8;
    } else if (region == 1) {
        tile_bank = ((bank_reg >> 12) & 0xF) << 9;
    } else {
        tile_bank = ((bank_reg >> 12) & 0x7) << 10;
    }

    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        int32_t cur_x = start_accx + (x * coef_a);
        int32_t cur_y = start_accy + (x * coef_c);
        int32_t src_x = cur_x >> 8;
        int32_t src_y = cur_y >> 8;

        bool outside = !wrap && (src_x < 0 || src_x >= x_limit || src_y < 0 || src_y >= y_limit);
        if (outside) continue;

        uint32_t mx = static_cast<uint32_t>(src_x) & (x_limit - 1);
        uint32_t my = static_cast<uint32_t>(src_y) & (y_limit - 1);

        uint8_t pixel_val = 0;
        bool opaque = false;
        uint8_t tie = 0;

        if (bitmap) {
            uint32_t bitmap_byte = (static_cast<uint32_t>(bank_reg) << 2) + (my << (xs_log + 3)) + mx;
            uint8_t b = read_vram_byte(bitmap_byte);
            if (b != 0) {
                pixel_val = b;
                opaque = true;
            }
        } else if (region == 4) {
            // Boot logo 1bpp
            uint32_t lcount = ((my & 0x700) << 2) | ((my & 0xF8) << 2) | (mx >> 3);
            uint32_t logo_tile = 0x880 + ((lcount & 7) << 1);
            if (lcount & 0x20) logo_tile ^= 1;
            logo_tile |= ((lcount >> 6) & 3) << 4;
            uint32_t logo_byte_raw = (logo_tile << 3) + (my & 7);
            uint32_t logo_byte = ((logo_byte_raw & 0xFFF80) << 0) | ((logo_byte_raw & 0xF) << 3) | ((logo_byte_raw >> 4) & 7);
            uint8_t b = read_vram_byte(logo_byte);
            uint8_t bit = (b >> (7 - (mx & 7))) & 1;
            if (bit != 0) {
                pixel_val = bit;
                opaque = true;
            }
        } else {
            uint32_t map_idx = ((my >> 3) << xs_log) | (mx >> 3);
            uint16_t map_entry = vram[(map_base_words + map_idx) & 0xFFFF];
            uint32_t tile_no = (map_entry & 0x3FF) + tile_bank;
            bool flip_v = (map_entry & (1 << 10)) != 0;
            bool flip_h = (map_entry & (1 << 11)) != 0;
            uint8_t tile_pal = static_cast<uint8_t>((map_entry >> 12) | ((tile_mode & (1 << 9)) ? 8 : 0));
            tie = ((tile_mode & (1 << 9)) && !(map_entry & 0x8000)) ? 1 : 0;

            int py = flip_v ? ((my & 7) ^ 7) : (my & 7);
            int px = flip_h ? ((mx & 7) ^ 7) : (mx & 7);

            if (region == 0) {
                // 8bpp
                uint32_t byte_addr = (tile_no * 64) + (py * 8) + px;
                uint8_t b = read_vram_byte(byte_addr);
                if (b != 0) {
                    pixel_val = b;
                    opaque = true;
                }
            } else if (region == 1) {
                // 4bpp
                uint32_t byte_addr = (tile_no * 32) + (py * 4) + (px >> 1);
                uint8_t b = read_vram_byte(byte_addr);
                uint8_t nib = (px & 1) ? (b & 0xF) : (b >> 4);
                if (nib != 0) {
                    pixel_val = (tile_pal << 4) | nib;
                    opaque = true;
                }
            } else {
                // 2bpp
                uint32_t byte_addr = (tile_no * 16) + (py * 2) + (px >> 2);
                uint8_t b = read_vram_byte(byte_addr);
                uint8_t shift = (3 - (px & 3)) * 2;
                uint8_t bits = (b >> shift) & 3;
                if (bits != 0) {
                    pixel_val = (tile_pal << 4) | bits; // 2bpp uses 16-color palette stride
                    opaque = true;
                }
            }
        }

        if (opaque && (prio <= prio_buf[x])) {
            line_buf[x] = pixel_val;
            prio_buf[x] = prio;
            tie_buf[x] = tie;
        }
    }
}

void PPU::render_scanline(int line) {
    if (line < 0 || line >= SCREEN_HEIGHT) return;

    std::array<uint8_t, SCREEN_WIDTH> line_buf;
    std::array<uint8_t, SCREEN_WIDTH> prio_buf;

    line_buf.fill(0);
    prio_buf.fill(0xFF); // lowest priority
    tie_buf.fill(0);

    uint16_t video_flags = regs[0x04]; // $F00008
    // Bit 8: 320 px lines, else 256 px (MiSTer um6618: tilemaps, ROZ and sprites all stop at
    // this width). Columns past it are blanked instead of showing the layers wrapping round.
    const int line_width = (video_flags & 0x0100) ? SCREEN_WIDTH : 256;
    if (line == 0) active_width = line_width;

    // Render tilemaps from lowest to highest priority
    // Layers 3, 2, 1, 0
    for (int l = 3; l >= 0; --l) {
        if (video_flags & (1 << (7 - l))) {
            render_tilemap_line(l, line, line_buf.data(), prio_buf.data());
        }
    }

    // Render ROZ if enabled
    if (video_flags & (1 << 2)) {
        render_roz_line(line, line_buf.data(), prio_buf.data());
    }

    // Render sprites if enabled
    if (video_flags & (1 << 3)) {
        render_sprites_line(line, line_buf.data(), prio_buf.data());
    }

    // Window clippers ($F001D0 / $F001D8): a horizontal span per line filled with one palette
    // index, used for iris wipes and screen fades. Edges come from a VRAM table (one pair per
    // frame, or per line when control bit 8 is set) plus the window's X scroll; bit 11 inverts
    // the span. Window 0 is enabled by video flags bit 1, window 1 by bit 0 (and a non-zero
    // control word). The FPGA fetches a line's edges during the previous line; we read them
    // directly, which only matters if the table changes mid-frame.
    for (int w = 0; w < 2; ++w) {
        bool enabled = (w == 0) ? (video_flags & (1 << 1)) != 0 : (video_flags & (1 << 0)) != 0;
        uint16_t ctrl = regs[0xE8 + w * 4];      // $1D0 / $1D8
        uint16_t base = regs[0xE9 + w * 4];      // $1D2 / $1DA
        uint16_t scrx = regs[0xEA + w * 4];      // $1D4 / $1DC
        if (!enabled || (w == 1 && ctrl == 0)) continue;

        int sx = scrx & 0x3FF;
        if (sx & 0x200) sx -= 0x400;
        uint32_t addr = ((static_cast<uint32_t>(base & 0x7FFF) << 1) + ((ctrl & 0x100) ? line * 2 : 0)) & 0xFFFF;
        int wmin = static_cast<int16_t>(vram[addr]) + sx;
        int wmax = static_cast<int16_t>(vram[(addr + 1) & 0xFFFF]) + sx;
        bool invert = (ctrl & (1 << 11)) != 0;
        uint8_t pen = static_cast<uint8_t>(ctrl & 0xFF);
        uint8_t wprio = (ctrl >> 13) & 3;

        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            bool inside = ((x >= wmin) && (x < wmax)) != invert;
            // Lower priority values are in front; on a tie the window never stands behind a
            // background or ROZ layer (MiSTer core: Sango Fighter's character-select close).
            if (inside && wprio <= prio_buf[x]) {
                line_buf[x] = pen;
                prio_buf[x] = wprio;
            }
        }
    }

    // Update palette cache if dirty
    if (palette_dirty) {
        for (int i = 0; i < 256; i++) {
            palette_cache[i] = color_to_argb(palette[i]);
        }
        palette_dirty = false;
    }

    // Convert palette indices to 32-bit ARGB using cache
    uint32_t* fb_line = &framebuffer[line * SCREEN_WIDTH];
    for (int x = 0; x < line_width; ++x) {
        uint8_t idx = line_buf[x];
        fb_line[x] = palette_cache[idx];
    }
    for (int x = line_width; x < SCREEN_WIDTH; ++x) fb_line[x] = 0xFF000000;
}
