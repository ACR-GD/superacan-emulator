#include "bus.h"
extern "C" {
#include "m68k.h"
}

// 68000 accesses to sound RAM ($E80000) go through the UM6619's arbiter, which serves the
// 65C02 first; each access stalls the 68000. Modelled as a fixed wait-state cost per byte.
// About 3 CPU clocks per byte: the arbiter reserves the RAM around each 65C02 phase, so a
// 68000 byte cycle typically waits ~2-4 clocks. Speedy Dragon's boot handshake needs the
// 68000's sound-RAM polling loop to be at least this slow (it times out otherwise).
constexpr int kSoundRamWait = 3;
static inline void stall_sound_ram(int bytes) { m68k_modify_timeslice(-kSoundRamWait * bytes); }
#include <fstream>
#include <iostream>
#include <cstring>
extern "C" {
#include "m68k.h"
}

Bus* g_bus = nullptr;

Bus::Bus() {
    g_bus = this;
    prg_rom.resize(0x200000, 0xFF); // 2 MB
    dat_rom.resize(0x100000, 0xFF); // 1 MB
    sram.fill(0xFF);                // blank battery RAM
    reset();
}

void Bus::reset() {
    wram.fill(0);
    apu.reset();

    irq_mask = 0;
    frc_ctrl = 0;
    frc_reload = 0;
    frc_value = 0;
    frc_sub = 0;
    frc_irq = false;

    // Without a boot ROM ("direct boot"), the overlay register powers up as if the BIOS had
    // already locked itself out (bits 3 and 1 set), mapping the cartridge at $000000.
    // With the boot ROM the console starts in its BIOS, as on the hardware.
    if (boot68k.empty()) {
        overlay_reg = 0x000A;
        lo_locked = hi_locked = true;
    } else {
        overlay_reg = 0;
        lo_locked = hi_locked = false;
    }
    addr6650 = 0x7F;
    ram6650.fill(0);
    egg_reg = 0;

    for (int i = 0; i < 2; ++i) {
        dma[i].src = 0;
        dma[i].dst = 0;
        dma[i].count = 0;
        dma[i].ctrl = 0;
    }

    ppu.reset();
    joypad.reset();
}

bool Bus::load_prg_buffer(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;

    size_t copy_size = std::min(size, prg_rom.size());
    std::memcpy(prg_rom.data(), data, copy_size);
    // Smaller cartridges (e.g. Boom Zoo, 512 KB) are not fully decoded: the ROM repeats
    // across the 2 MB program window, as on the hardware (MAME mirrors it the same way).
    for (size_t off = copy_size; off + copy_size <= prg_rom.size(); off += copy_size) {
        std::memcpy(prg_rom.data() + off, data, copy_size);
    }

    // Auto-detect byte order from the initial stack pointer, which every game places in work
    // RAM ($FC0000-$FFFFFF): its high word is $00FC-$00FF. A word-swapped dump (the usual
    // MAME/No-Intro format) therefore starts "FC..FF 00", a plain big-endian one "00 FC..FF".
    // (Checking only for "FF 00" missed every game whose stack starts at $FCxxxx.)
    if (prg_rom[1] == 0x00 && prg_rom[0] >= 0xFC) {
        std::cout << "[Bus] PRG ROM is byte-swapped; swapping to 68k big-endian order.\n";
        prg_byteswapped = true;
        for (size_t i = 0; i < prg_rom.size(); i += 2) {
            std::swap(prg_rom[i], prg_rom[i + 1]);
        }
    } else {
        std::cout << "[Bus] PRG ROM is standard big-endian order.\n";
        prg_byteswapped = false;
    }

    // Single-file dumps of 2+1 MB cartridges (e.g. Sango Fighter, 3 MB) carry the data ROM
    // after the program ROM; MAME maps the same file as one 3 MB area. Load the rest as DAT.
    if (size > prg_rom.size()) {
        load_dat_buffer(data + prg_rom.size(), size - prg_rom.size());
    }

    std::cout << "[Bus] PRG ROM loaded. Initial SSP: 0x" << std::hex << get_initial_ssp()
              << " PC: 0x" << get_initial_pc() << std::dec << "\n";
    return true;
}

bool Bus::load_dat_buffer(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;

    size_t copy_size = std::min(size, dat_rom.size());
    std::memcpy(dat_rom.data(), data, copy_size);

    // The DAT ROM has no header to detect byte order from, so it follows the PRG ROM:
    // dumps (MAME / No-Intro format) store both chips with the same 16-bit word swap.
    // Must be loaded after the PRG ROM.
    if (prg_byteswapped) {
        std::cout << "[Bus] DAT ROM is byte-swapped; swapping to 68k big-endian order.\n";
        for (size_t i = 0; i < dat_rom.size(); i += 2) {
            std::swap(dat_rom[i], dat_rom[i + 1]);
        }
    }
    std::cout << "[Bus] DAT ROM loaded (" << copy_size << " bytes).\n";
    return true;
}

bool Bus::load_prg_rom(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[Bus] Failed to open PRG ROM: " << path << "\n";
        return false;
    }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    return load_prg_buffer(buf.data(), buf.size());
}

bool Bus::load_dat_rom(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[Bus] Failed to open DAT ROM: " << path << "\n";
        return false;
    }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    return load_dat_buffer(buf.data(), buf.size());
}

uint32_t Bus::get_initial_ssp() const {
    if (prg_rom.size() < 4) return 0;
    return (prg_rom[0] << 24) | (prg_rom[1] << 16) | (prg_rom[2] << 8) | prg_rom[3];
}

uint32_t Bus::get_initial_pc() const {
    if (prg_rom.size() < 8) return 0;
    return (prg_rom[4] << 24) | (prg_rom[5] << 16) | (prg_rom[6] << 8) | prg_rom[7];
}

void Bus::execute_host_dma(int ch) {
    uint32_t s = dma[ch].src;
    uint32_t d = dma[ch].dst;
    uint32_t count = dma[ch].count + 1;
    uint16_t c = dma[ch].ctrl;

    bool src_fixed = ((c & 0x1000) == 0);
    bool src_down  = ((c & 0x0200) != 0);
    bool dst_down  = ((c & 0x0400) != 0);
    bool wrap16    = ((c & 0x0100) != 0);

    for (uint32_t i = 0; i < count; ++i) {
        uint16_t val = read16(s);
        write16(d, val);

        if (!src_fixed) {
            if (src_down) s -= 2;
            else s += 2;
        }

        if (dst_down) {
            d -= 2;
        } else {
            d += 2;
        }

        if (wrap16 && ((d & 0xF) == 0)) {
            d -= 0x10;
        }
    }

    dma[ch].src = s;
    dma[ch].dst = d;
    dma[ch].count = 0;
    dma[ch].ctrl &= ~0x8000;
}

void Bus::execute_video_dma() {
    // Sprite DMA parameters from UM6618 video registers 0x10..0x1E:
    // $F00010: count
    // $F00012: dst[31:16]
    // $F00014: dst[15:0]
    // $F00016: dst_inc
    // $F00018: src[31:16]
    // $F0001A: src[15:0]
    // $F0001C: src_inc
    // $F0001E: ctrl
    uint16_t cnt_val = ppu.read_reg(0x10);
    uint32_t dst = (static_cast<uint32_t>(ppu.read_reg(0x12)) << 16) | ppu.read_reg(0x14);
    uint16_t dst_inc = ppu.read_reg(0x16);
    uint32_t src = (static_cast<uint32_t>(ppu.read_reg(0x18)) << 16) | ppu.read_reg(0x1A);
    uint16_t src_inc = ppu.read_reg(0x1C);
    uint16_t ctrl = ppu.read_reg(0x1E);

    uint32_t total = cnt_val + 1;
    bool fill = (ctrl & 0x0100) != 0;

    // Destination: bits 13 and 14 force into VRAM
    if ((ctrl & 0x6000) != 0 || dst < 0xF40000) {
        dst |= 0xF40000;
    }

    for (uint32_t i = 0; i < total; ++i) {
        uint16_t val = fill ? 0 : read16(src);
        write16(dst, val);

        if (!fill) {
            src += src_inc * 2;
        }
        dst += dst_inc * 2;
    }

    // Write back final pointers per MiSTer RTL
    ppu.write_reg(0x12, static_cast<uint16_t>(dst >> 16));
    ppu.write_reg(0x14, static_cast<uint16_t>(dst & 0xFFFF));
    ppu.write_reg(0x18, static_cast<uint16_t>(src >> 16));
    ppu.write_reg(0x1A, static_cast<uint16_t>(src & 0xFFFF));
    ppu.write_reg(0x1E, ctrl & ~0x8000);
}

uint8_t Bus::read8(uint32_t addr) {
    addr &= 0x00FFFFFF;

    // Battery SRAM: low byte lane only
    if (is_sram(addr)) {
        return (addr & 1) ? sram[sram_index(addr)] : 0xFF;
    }
    if (!lo_locked && addr < 0x1000) return boot68k[addr];
    if (addr >= 0xF80000 && addr < 0xFC0000) {
        if (!hi_locked && addr < 0xF81000) return boot68k[addr - 0xF80000];
        return prg_rom[(addr - 0xF80000) & 0x1FFFFF];
    }
    if ((addr & 0xFFFFFC) == 0xEB0D00) return (addr & 1) ? read6650(addr) : 0xFF;

    // Cartridge PRG ROM: 0x000000 - 0x1FFFFF
    if (addr < 0x200000) {
        return prg_rom[addr];
    }
    // Cartridge DAT ROM: 0x200000 - 0x3FFFFF
    if (addr >= 0x200000 && addr < 0x400000) {
        uint32_t offset = addr - 0x200000;
        if (offset < dat_rom.size()) {
            return dat_rom[offset];
        }
        return 0xFF;
    }
    // Sound RAM: 0xE80000 - 0xE8FFFF
    if (addr >= 0xE80000 && addr < 0xE90000) {
        stall_sound_ram(1);
        uint32_t offset = addr - 0xE80000;
        // Dynamically provide joypad input at 0xE80200
        if (offset == 0x0200) {
            uint16_t p1 = joypad.get_buttons(0);
            return static_cast<uint8_t>(p1 >> 8);
        }
        if (offset == 0x0201) {
            uint16_t p1 = joypad.get_buttons(0);
            return static_cast<uint8_t>(p1 & 0xFF);
        }
        if (offset == 0x0202) {
            uint16_t p2 = joypad.get_buttons(1);
            return static_cast<uint8_t>(p2 >> 8);
        }
        if (offset == 0x0203) {
            uint16_t p2 = joypad.get_buttons(1);
            return static_cast<uint8_t>(p2 & 0xFF);
        }
        return apu.read_sound_ram(offset);
    }
    // System / Host registers: 0xE90000 - 0xE9FFFF
    if (addr >= 0xE90000 && addr < 0xEA0000) {
        uint32_t reg_offset = addr & 0x0000FFFF;
        if (reg_offset == 0x0004) return apu.get_mailbox_c();
        if (reg_offset == 0x0005) return apu.get_mailbox_d();
        if (reg_offset == 0x000C) return apu.get_mailbox_a();
        uint16_t w = read16(addr & ~1);
        return (addr & 1) ? static_cast<uint8_t>(w & 0xFF) : static_cast<uint8_t>(w >> 8);
    }
    // Video Registers: 0xF00000 - 0xF001FF
    if (addr >= 0xF00000 && addr < 0xF00200) {
        uint16_t w = ppu.read_reg(addr - 0xF00000);
        return (addr & 1) ? static_cast<uint8_t>(w & 0xFF) : static_cast<uint8_t>(w >> 8);
    }
    // Palette RAM: 0xF00200 - 0xF003FF
    if (addr >= 0xF00200 && addr < 0xF00400) {
        uint16_t w = ppu.read_palette(addr - 0xF00200);
        return (addr & 1) ? static_cast<uint8_t>(w & 0xFF) : static_cast<uint8_t>(w >> 8);
    }
    // VRAM: 0xF40000 - 0xF5FFFF
    if (addr >= 0xF40000 && addr < 0xF60000) {
        return ppu.read_vram_byte(addr - 0xF40000);
    }
    // Work RAM: 0xFC0000 - 0xFFFFFF (64 KB mirrored)
    if (addr >= 0xFC0000) {
        return wram[addr & 0xFFFF];
    }

    return 0xFF;
}

uint16_t Bus::read16(uint32_t addr) {
    addr &= 0x00FFFFFF;

    if (is_sram(addr)) {
        return 0xFF00 | sram[sram_index(addr)];
    }
    if (!lo_locked && addr < 0x1000) return (boot68k[addr] << 8) | boot68k[addr + 1];
    if (addr >= 0xF80000 && addr < 0xFC0000) {
        if (!hi_locked && addr < 0xF81000) return (boot68k[addr - 0xF80000] << 8) | boot68k[addr - 0xF80000 + 1];
        uint32_t o = (addr - 0xF80000) & 0x1FFFFF;
        return (prg_rom[o] << 8) | prg_rom[o + 1];
    }
    if ((addr & 0xFFFFFC) == 0xEB0D00) return 0xFF00 | read6650(addr | 1);

    // Cartridge PRG ROM: 0x000000 - 0x1FFFFF
    if (addr < 0x200000) {
        return (prg_rom[addr] << 8) | prg_rom[addr + 1];
    }
    // Cartridge DAT ROM: 0x200000 - 0x3FFFFF
    if (addr >= 0x200000 && addr < 0x400000) {
        uint32_t offset = addr - 0x200000;
        if (offset + 1 < dat_rom.size()) {
            return (dat_rom[offset] << 8) | dat_rom[offset + 1];
        }
        return 0xFFFF;
    }
    // Sound RAM: 0xE80000 - 0xE8FFFF
    if (addr >= 0xE80000 && addr < 0xE90000) {
        stall_sound_ram(2);
        uint32_t offset = addr - 0xE80000;
        if (offset == 0x0200) {
            return joypad.get_buttons(0);
        }
        if (offset == 0x0202) {
            return joypad.get_buttons(1);
        }
        return (static_cast<uint16_t>(apu.read_sound_ram(offset)) << 8) | apu.read_sound_ram(offset + 1);
    }
    // System / Host registers: 0xE90000 - 0xE9FFFF
    if (addr >= 0xE90000 && addr < 0xEA0000) {
        uint32_t reg_offset = addr & 0x0000FFFF;
        if (reg_offset == 0x0004) {
            return (static_cast<uint16_t>(apu.get_mailbox_c()) << 8) | apu.get_mailbox_d();
        }
        if (reg_offset == 0x000C) {
            return static_cast<uint16_t>(apu.get_mailbox_a()) << 8;
        }
        if (reg_offset == 0x0010) return static_cast<uint16_t>((irq_mask << 8) | irq_mask);
        if (reg_offset == 0x0014) return frc_ctrl;
        if (reg_offset == 0x0016) return frc_reload;
        if (reg_offset == 0x0018) return frc_value;
        if (reg_offset == 0x001C) return overlay_reg;
        if (reg_offset == 0x0B3C) return egg_reg;

        // DMA registers E90020-E9003F
        if (reg_offset >= 0x0020 && reg_offset < 0x0040) {
            int ch = (reg_offset >= 0x0030) ? 1 : 0;
            uint32_t ch_off = reg_offset & 0xF;
            switch (ch_off) {
                case 0x0: return static_cast<uint16_t>(dma[ch].src >> 16);
                case 0x2: return static_cast<uint16_t>(dma[ch].src & 0xFFFF);
                case 0x4: return static_cast<uint16_t>(dma[ch].dst >> 16);
                case 0x6: return static_cast<uint16_t>(dma[ch].dst & 0xFFFF);
                case 0x8: return dma[ch].count;
                case 0xA: return dma[ch].ctrl;
                default: return 0;
            }
        }
        return 0;
    }
    // Video Registers: 0xF00000 - 0xF001FF
    if (addr >= 0xF00000 && addr < 0xF00200) {
        return ppu.read_reg(addr - 0xF00000);
    }
    // Palette RAM: 0xF00200 - 0xF003FF
    if (addr >= 0xF00200 && addr < 0xF00400) {
        return ppu.read_palette(addr - 0xF00200);
    }
    // VRAM: 0xF40000 - 0xF5FFFF
    if (addr >= 0xF40000 && addr < 0xF60000) {
        return ppu.read_vram(addr - 0xF40000);
    }
    // Work RAM: 0xFC0000 - 0xFFFFFF (64 KB mirrored)
    if (addr >= 0xFC0000) {
        uint32_t offset = addr & 0xFFFF;
        return (wram[offset] << 8) | wram[offset + 1];
    }

    return 0xFFFF;
}

uint32_t Bus::read32(uint32_t addr) {
    return (static_cast<uint32_t>(read16(addr)) << 16) | read16(addr + 2);
}

void Bus::write8(uint32_t addr, uint8_t val) {
    addr &= 0x00FFFFFF;

    if ((addr & 0xFFFFFC) == 0xEB0D00) { if (addr & 1) write6650(addr, val); return; }
    if (is_sram(addr)) {
        if (addr & 1) {
            uint8_t& cell = sram[sram_index(addr)];
            if (cell != val) { cell = val; sram_dirty = true; }
        }
        return;
    }

    // Sound RAM: 0xE80000 - 0xE8FFFF
    if (addr >= 0xE80000 && addr < 0xE90000) {
        stall_sound_ram(1);
        uint32_t offset = addr - 0xE80000;
        apu.host_write(static_cast<uint16_t>(offset), val);
        return;
    }
    // System / Host registers
    if (addr >= 0xE90000 && addr < 0xEA0000) {
        if ((addr & 0xFFFF) == 0x000A) {
            apu.write_cmd();
            return;
        }
        // $E90010 interrupt mask: one 8-bit register mirrored on both byte lanes (games write
        // either byte; MAME's "smearing mirror"). Sango Fighter enables its line interrupt by
        // writing $B8 to the high byte.
        if ((addr & 0xFFFE) == 0x0010) {
            irq_mask = val;
            return;
        }
        uint32_t reg_addr = addr & ~1;
        uint16_t cur = read16(reg_addr);
        if (addr & 1) {
            cur = (cur & 0xFF00) | val;
        } else {
            cur = (cur & 0x00FF) | (static_cast<uint16_t>(val) << 8);
        }
        write16(reg_addr, cur);
        return;
    }
    // Video Registers: 0xF00000 - 0xF001FF
    if (addr >= 0xF00000 && addr < 0xF00200) {
        uint32_t reg_addr = (addr - 0xF00000) & ~1;
        uint16_t cur = ppu.read_reg(reg_addr);
        if (addr & 1) {
            cur = (cur & 0xFF00) | val;
        } else {
            cur = (cur & 0x00FF) | (static_cast<uint16_t>(val) << 8);
        }
        ppu.write_reg(reg_addr, cur);
        if (reg_addr == 0x1E && (cur & 0x8000)) {
            execute_video_dma();
        }
        return;
    }
    // Palette RAM: 0xF00200 - 0xF003FF
    if (addr >= 0xF00200 && addr < 0xF00400) {
        uint32_t pal_addr = (addr - 0xF00200) & ~1;
        uint16_t cur = ppu.read_palette(pal_addr);
        if (addr & 1) {
            cur = (cur & 0xFF00) | val;
        } else {
            cur = (cur & 0x00FF) | (static_cast<uint16_t>(val) << 8);
        }
        ppu.write_palette(pal_addr, cur);
        return;
    }
    // VRAM: 0xF40000 - 0xF5FFFF
    if (addr >= 0xF40000 && addr < 0xF60000) {
        ppu.write_vram_byte(addr - 0xF40000, val);
        return;
    }
    // Work RAM: 0xFC0000 - 0xFFFFFF
    if (addr >= 0xFC0000) {
        wram[addr & 0xFFFF] = val;
        return;
    }
}

void Bus::write16(uint32_t addr, uint16_t val) {
    addr &= 0x00FFFFFF;

    if ((addr & 0xFFFFFC) == 0xEB0D00) { write6650(addr | 1, static_cast<uint8_t>(val)); return; }
    if (is_sram(addr)) {
        uint8_t& cell = sram[sram_index(addr)];
        uint8_t b = static_cast<uint8_t>(val & 0xFF);
        if (cell != b) { cell = b; sram_dirty = true; }
        return;
    }

    // Sound RAM: 0xE80000 - 0xE8FFFF
    if (addr >= 0xE80000 && addr < 0xE90000) {
        stall_sound_ram(2);
        uint32_t offset = addr - 0xE80000;
        apu.host_write(static_cast<uint16_t>(offset), static_cast<uint8_t>(val >> 8));
        apu.host_write(static_cast<uint16_t>(offset + 1), static_cast<uint8_t>(val & 0xFF));
        return;
    }
    // System / Host registers: 0xE90000 - 0xE9FFFF
    if (addr >= 0xE90000 && addr < 0xEA0000) {
        uint32_t reg_offset = addr & 0x0000FFFF;
        if (reg_offset == 0x000A) {
            apu.write_cmd();
            return;
        }
        if (reg_offset == 0x0010) {
            irq_mask = val & 0xFF;
            return;
        }
        if (reg_offset == 0x0014) {
            frc_ctrl = val;
            // Starting the counter loads it from the period register.
            if ((val >> 8) == 0xA2 || val == 0xA300) { frc_value = frc_reload; frc_sub = 0; }
            return;
        }
        if (reg_offset == 0x0016) {
            frc_reload = val;
            return;
        }
        if (reg_offset == 0x001C) {
            // Setting bit 3 / bit 1 locks the boot ROM out of $F80000 / $000000 for good.
            if (((overlay_reg ^ val) & val) & 0x8) hi_locked = true;
            if (((overlay_reg ^ val) & val) & 0x2) lo_locked = true;
            overlay_reg = val;
            apu.set_cpu_reset((val & 1) != 0);
            return;
        }
        if (reg_offset == 0x0B3C) {
            egg_reg = val;
            return;
        }

        // DMA registers E90020-E9003F
        if (reg_offset >= 0x0020 && reg_offset < 0x0040) {
            int ch = (reg_offset >= 0x0030) ? 1 : 0;
            uint32_t ch_off = reg_offset & 0xF;
            switch (ch_off) {
                case 0x0: dma[ch].src = (dma[ch].src & 0x0000FFFF) | (static_cast<uint32_t>(val) << 16); break;
                case 0x2: dma[ch].src = (dma[ch].src & 0xFFFF0000) | val; break;
                case 0x4: dma[ch].dst = (dma[ch].dst & 0x0000FFFF) | (static_cast<uint32_t>(val) << 16); break;
                case 0x6: dma[ch].dst = (dma[ch].dst & 0xFFFF0000) | val; break;
                case 0x8: dma[ch].count = val; break;
                case 0xA: {
                    dma[ch].ctrl = val;
                    if (val & 0x8000) {
                        execute_host_dma(ch);
                    }
                    break;
                }
            }
            return;
        }
        return;
    }
    // Video Registers: 0xF00000 - 0xF001FF
    if (addr >= 0xF00000 && addr < 0xF00200) {
        uint32_t reg_addr = addr - 0xF00000;
        ppu.write_reg(reg_addr, val);
        // Sprite DMA trigger at $F0001E bit 15
        if (reg_addr == 0x1E && (val & 0x8000)) {
            execute_video_dma();
        }
        return;
    }
    // Palette RAM: 0xF00200 - 0xF003FF
    if (addr >= 0xF00200 && addr < 0xF00400) {
        ppu.write_palette(addr - 0xF00200, val);
        return;
    }
    // VRAM: 0xF40000 - 0xF5FFFF
    if (addr >= 0xF40000 && addr < 0xF60000) {
        ppu.write_vram(addr - 0xF40000, val);
        return;
    }
    // Work RAM: 0xFC0000 - 0xFFFFFF
    if (addr >= 0xFC0000) {
        uint32_t offset = addr & 0xFFFF;
        wram[offset] = static_cast<uint8_t>(val >> 8);
        wram[offset + 1] = static_cast<uint8_t>(val & 0xFF);
        return;
    }
}

void Bus::write32(uint32_t addr, uint32_t val) {
    write16(addr, static_cast<uint16_t>(val >> 16));
    write16(addr + 2, static_cast<uint16_t>(val & 0xFFFF));
}

// Free running counter, after the MiSTer core's um6619_frc:
//   $A2x0  mode 0: fires once a second, ignores the period
//   $A2x1  mode 1: counts 1024 CPU cycles per step, fires every (n+1) steps
//   $A2xF  mode F: counts 9040 CPU cycles per step, fires every (n+1) steps
//   $A300        : counts video frames, fires every (n+1) frames
// CPU_CLK is 10.738635 MHz. Other modes are unknown and stay idle.
static bool frc_enabled(uint16_t ctrl) {
    uint8_t mode = ctrl & 0xF;
    return ((ctrl >> 8) == 0xA2 && (mode == 0x0 || mode == 0x1 || mode == 0xF));
}

void Bus::step_frc(int cpu_cycles) {
    if (!frc_enabled(frc_ctrl)) return;
    uint8_t mode = frc_ctrl & 0xF;
    const uint32_t unit = (mode == 0x0) ? 10738635u : (mode == 0xF) ? 9040u : 1024u;
    frc_sub += static_cast<uint32_t>(cpu_cycles);
    while (frc_sub >= unit) {
        frc_sub -= unit;
        if (mode == 0x0) {
            frc_irq = true;
        } else if (frc_value == 0) {
            frc_value = frc_reload;
            frc_irq = true;
        } else {
            --frc_value;
        }
    }
}

void Bus::frc_vblank() {
    if (frc_ctrl != 0xA300) return;
    if (frc_value == 0) { frc_value = frc_reload; frc_irq = true; }
    else --frc_value;
}

void Bus::set_boot_rom(const std::vector<uint8_t>& rom68k, const std::vector<uint8_t>& key) {
    boot68k = rom68k;
    for (size_t i = 0; i + 1 < boot68k.size(); i += 2) std::swap(boot68k[i], boot68k[i + 1]);  // stored word-swapped
    boot68k.resize(0x1000, 0xFF);
    for (size_t i = 0; i < key6650.size(); ++i) key6650[i] = i < key.size() ? key[i] : 0xFF;
}

// UM6650 lockout chip in the cartridge ($EB0D00-$EB0D03, low byte lane): register 1 selects
// an internal address, register 0 reads/writes it. $20-$2F hold the 16-byte key, $40-$5F are
// RAM; everything else reads 0 (after MAME's umc6650 device).
uint8_t Bus::read6650(uint32_t addr) {
    if ((addr & 2) != 0) return addr6650;
    if (addr6650 >= 0x20 && addr6650 < 0x30) return key6650[addr6650 - 0x20];
    if (addr6650 >= 0x40 && addr6650 < 0x60) return ram6650[addr6650 - 0x40];
    return 0;
}

void Bus::write6650(uint32_t addr, uint8_t val) {
    if ((addr & 2) != 0) { addr6650 = val & 0x7F; return; }
    if (addr6650 >= 0x40 && addr6650 < 0x60) ram6650[addr6650 - 0x40] = val;
}
