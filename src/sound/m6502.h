#pragma once
#include <cstdint>
#include <functional>

class M6502 {
public:
    enum StatusFlag : uint8_t {
        FLAG_C = 0x01, // Carry
        FLAG_Z = 0x02, // Zero
        FLAG_I = 0x04, // Interrupt Disable
        FLAG_D = 0x08, // Decimal Mode
        FLAG_B = 0x10, // Break Command
        FLAG_U = 0x20, // Unused / Always 1
        FLAG_V = 0x40, // Overflow
        FLAG_N = 0x80  // Negative
    };

    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t sp = 0xFD;
    uint8_t p = 0x24; // FLAG_U | FLAG_I
    uint16_t pc = 0;

    bool irq_line = false;
    bool nmi_line = false;
    bool nmi_pending = false;

    // Callbacks for bus access
    std::function<uint8_t(uint16_t)> read_fn;
    std::function<void(uint16_t, uint8_t)> write_fn;

    M6502() = default;

    inline uint8_t read(uint16_t addr) {
        return read_fn ? read_fn(addr) : 0xFF;
    }

    inline void write(uint16_t addr, uint8_t val) {
        if (write_fn) write_fn(addr, val);
    }

    inline uint16_t read16(uint16_t addr) {
        uint8_t lo = read(addr);
        uint8_t hi = read(addr + 1);
        return (static_cast<uint16_t>(hi) << 8) | lo;
    }

    inline void push(uint8_t val) {
        write(0x0100 | sp, val);
        sp--;
    }

    inline uint8_t pull() {
        sp++;
        return read(0x0100 | sp);
    }

    void reset() {
        a = 0;
        x = 0;
        y = 0;
        sp = 0xFD;
        p = FLAG_U | FLAG_I;
        uint8_t lo = read(0xFFFC);
        uint8_t hi = read(0xFFFD);
        pc = (static_cast<uint16_t>(hi) << 8) | lo;
        irq_line = false;
        nmi_line = false;
        nmi_pending = false;
    }

    void set_irq(bool asserted) {
        irq_line = asserted;
    }

    void trigger_nmi() {
        nmi_pending = true;
    }

    inline void set_nz(uint8_t val) {
        p = (p & ~(FLAG_N | FLAG_Z)) | (val == 0 ? FLAG_Z : 0) | (val & FLAG_N);
    }

    inline void set_c(bool c) {
        p = (p & ~FLAG_C) | (c ? FLAG_C : 0);
    }

    inline void set_v(bool v) {
        p = (p & ~FLAG_V) | (v ? FLAG_V : 0);
    }

    int step() {
        if (nmi_pending) {
            nmi_pending = false;
            push(static_cast<uint8_t>(pc >> 8));
            push(static_cast<uint8_t>(pc & 0xFF));
            push(p | FLAG_U);
            p |= FLAG_I;
            pc = read16(0xFFFA);
            return 7;
        }

        if (irq_line && !(p & FLAG_I)) {
            push(static_cast<uint8_t>(pc >> 8));
            push(static_cast<uint8_t>(pc & 0xFF));
            push((p | FLAG_U) & ~FLAG_B);
            p |= FLAG_I;
            pc = read16(0xFFFE);
            return 7;
        }

        uint8_t op = read(pc++);
        int cycles = 2;

        auto fetch_byte = [&]() -> uint8_t {
            return read(pc++);
        };

        auto fetch_word = [&]() -> uint16_t {
            uint8_t lo = read(pc++);
            uint8_t hi = read(pc++);
            return (static_cast<uint16_t>(hi) << 8) | lo;
        };

        // Addressing modes
        auto addr_zp = [&]() -> uint16_t {
            return fetch_byte();
        };

        auto addr_zpx = [&]() -> uint16_t {
            return static_cast<uint8_t>(fetch_byte() + x);
        };

        auto addr_zpy = [&]() -> uint16_t {
            return static_cast<uint8_t>(fetch_byte() + y);
        };

        auto addr_abs = [&]() -> uint16_t {
            return fetch_word();
        };

        auto addr_absx = [&]() -> uint16_t {
            return fetch_word() + x;
        };

        auto addr_absy = [&]() -> uint16_t {
            return fetch_word() + y;
        };

        auto addr_indx = [&]() -> uint16_t {
            uint8_t zp = static_cast<uint8_t>(fetch_byte() + x);
            uint8_t lo = read(zp);
            uint8_t hi = read(static_cast<uint8_t>(zp + 1));
            return (static_cast<uint16_t>(hi) << 8) | lo;
        };

        auto addr_indy = [&]() -> uint16_t {
            uint8_t zp = fetch_byte();
            uint8_t lo = read(zp);
            uint8_t hi = read(static_cast<uint8_t>(zp + 1));
            return ((static_cast<uint16_t>(hi) << 8) | lo) + y;
        };

        auto addr_zpind = [&]() -> uint16_t {        // (zp), 65C02
            uint8_t zp = fetch_byte();
            return static_cast<uint16_t>(read(zp) | (read(static_cast<uint8_t>(zp + 1)) << 8));
        };

        auto do_bit = [&](uint8_t m) {
            if (a & m) p &= ~FLAG_Z; else p |= FLAG_Z;
            p = static_cast<uint8_t>((p & ~(FLAG_N | FLAG_V)) | (m & (FLAG_N | FLAG_V)));
        };

        auto do_branch = [&](bool cond) {
            int8_t offset = static_cast<int8_t>(fetch_byte());
            if (cond) {
                cycles += 1;
                uint16_t old_pc = pc;
                pc += offset;
                if ((pc & 0xFF00) != (old_pc & 0xFF00)) cycles += 1;
            }
        };

        auto do_adc = [&](uint8_t val) {
            if (p & FLAG_D) {
                int al = (a & 0x0F) + (val & 0x0F) + (p & FLAG_C ? 1 : 0);
                int ah = (a >> 4) + (val >> 4) + (al > 9 ? 1 : 0);
                if (al > 9) al += 6;
                set_nz(static_cast<uint8_t>((a + val + (p & FLAG_C ? 1 : 0)) & 0xFF));
                set_v((~(a ^ val) & (a ^ (ah << 4))) & 0x80);
                if (ah > 9) ah += 6;
                set_c(ah > 15);
                a = static_cast<uint8_t>(((ah & 0x0F) << 4) | (al & 0x0F));
            } else {
                int res = a + val + (p & FLAG_C ? 1 : 0);
                set_v((~(a ^ val) & (a ^ res)) & 0x80);
                set_c(res > 0xFF);
                set_nz(static_cast<uint8_t>(res & 0xFF));
                a = static_cast<uint8_t>(res & 0xFF);
            }
        };

        auto do_sbc = [&](uint8_t val) {
            if (p & FLAG_D) {
                int diff = a - val - (p & FLAG_C ? 0 : 1);
                int al = (a & 0x0F) - (val & 0x0F) - (p & FLAG_C ? 0 : 1);
                int ah = (a >> 4) - (val >> 4) - (al < 0 ? 1 : 0);
                if (al < 0) al -= 6;
                if (ah < 0) ah -= 6;
                set_v(((a ^ val) & (a ^ diff)) & 0x80);
                set_c(diff >= 0);
                set_nz(static_cast<uint8_t>(diff & 0xFF));
                a = static_cast<uint8_t>(((ah & 0x0F) << 4) | (al & 0x0F));
            } else {
                int diff = a - val - (p & FLAG_C ? 0 : 1);
                set_v(((a ^ val) & (a ^ diff)) & 0x80);
                set_c(diff >= 0);
                set_nz(static_cast<uint8_t>(diff & 0xFF));
                a = static_cast<uint8_t>(diff & 0xFF);
            }
        };

        auto do_cmp = [&](uint8_t reg, uint8_t val) {
            int diff = reg - val;
            set_c(diff >= 0);
            set_nz(static_cast<uint8_t>(diff & 0xFF));
        };

        switch (op) {
            // NOP
            case 0xEA: cycles = 2; break;

            // Load A
            case 0xA9: a = fetch_byte(); set_nz(a); cycles = 2; break;
            case 0xA5: a = read(addr_zp()); set_nz(a); cycles = 3; break;
            case 0xB5: a = read(addr_zpx()); set_nz(a); cycles = 4; break;
            case 0xAD: a = read(addr_abs()); set_nz(a); cycles = 4; break;
            case 0xBD: a = read(addr_absx()); set_nz(a); cycles = 4; break;
            case 0xB9: a = read(addr_absy()); set_nz(a); cycles = 4; break;
            case 0xA1: a = read(addr_indx()); set_nz(a); cycles = 6; break;
            case 0xB1: a = read(addr_indy()); set_nz(a); cycles = 5; break;

            // Load X
            case 0xA2: x = fetch_byte(); set_nz(x); cycles = 2; break;
            case 0xA6: x = read(addr_zp()); set_nz(x); cycles = 3; break;
            case 0xB6: x = read(addr_zpy()); set_nz(x); cycles = 4; break;
            case 0xAE: x = read(addr_abs()); set_nz(x); cycles = 4; break;
            case 0xBE: x = read(addr_absy()); set_nz(x); cycles = 4; break;

            // Load Y
            case 0xA0: y = fetch_byte(); set_nz(y); cycles = 2; break;
            case 0xA4: y = read(addr_zp()); set_nz(y); cycles = 3; break;
            case 0xB4: y = read(addr_zpx()); set_nz(y); cycles = 4; break;
            case 0xAC: y = read(addr_abs()); set_nz(y); cycles = 4; break;
            case 0xBC: y = read(addr_absx()); set_nz(y); cycles = 4; break;

            // Store A
            case 0x85: write(addr_zp(), a); cycles = 3; break;
            case 0x95: write(addr_zpx(), a); cycles = 4; break;
            case 0x8D: write(addr_abs(), a); cycles = 4; break;
            case 0x9D: write(addr_absx(), a); cycles = 5; break;
            case 0x99: write(addr_absy(), a); cycles = 5; break;
            case 0x81: write(addr_indx(), a); cycles = 6; break;
            case 0x91: write(addr_indy(), a); cycles = 6; break;

            // Store X
            case 0x86: write(addr_zp(), x); cycles = 3; break;
            case 0x96: write(addr_zpy(), x); cycles = 4; break;
            case 0x8E: write(addr_abs(), x); cycles = 4; break;

            // Store Y
            case 0x84: write(addr_zp(), y); cycles = 3; break;
            case 0x94: write(addr_zpx(), y); cycles = 4; break;
            case 0x8C: write(addr_abs(), y); cycles = 4; break;

            // STZ (65C02 store zero)
            case 0x64: write(addr_zp(), 0); cycles = 3; break;
            case 0x74: write(addr_zpx(), 0); cycles = 4; break;
            case 0x9C: write(addr_abs(), 0); cycles = 4; break;
            case 0x9E: write(addr_absx(), 0); cycles = 5; break;

            // Transfers
            case 0xAA: x = a; set_nz(x); cycles = 2; break; // TAX
            case 0x8A: a = x; set_nz(a); cycles = 2; break; // TXA
            case 0xA8: y = a; set_nz(y); cycles = 2; break; // TAY
            case 0x98: a = y; set_nz(a); cycles = 2; break; // TYA
            case 0xBA: x = sp; set_nz(x); cycles = 2; break;// TSX
            case 0x9A: sp = x; cycles = 2; break;          // TXS

            // Stack
            case 0x48: push(a); cycles = 3; break;          // PHA
            case 0x68: a = pull(); set_nz(a); cycles = 4; break; // PLA
            case 0x08: push(p | FLAG_B | FLAG_U); cycles = 3; break; // PHP
            case 0x28: p = (pull() & ~FLAG_B) | FLAG_U; cycles = 4; break; // PLP
            case 0xDA: push(x); cycles = 3; break;          // PHX (65C02)
            case 0xFA: x = pull(); set_nz(x); cycles = 4; break; // PLX (65C02)
            case 0x5A: push(y); cycles = 3; break;          // PHY (65C02)
            case 0x7A: y = pull(); set_nz(y); cycles = 4; break; // PLY (65C02)

            // ADC
            case 0x69: do_adc(fetch_byte()); cycles = 2; break;
            case 0x65: do_adc(read(addr_zp())); cycles = 3; break;
            case 0x75: do_adc(read(addr_zpx())); cycles = 4; break;
            case 0x6D: do_adc(read(addr_abs())); cycles = 4; break;
            case 0x7D: do_adc(read(addr_absx())); cycles = 4; break;
            case 0x79: do_adc(read(addr_absy())); cycles = 4; break;
            case 0x61: do_adc(read(addr_indx())); cycles = 6; break;
            case 0x71: do_adc(read(addr_indy())); cycles = 5; break;

            // SBC
            case 0xE9: do_sbc(fetch_byte()); cycles = 2; break;
            case 0xE5: do_sbc(read(addr_zp())); cycles = 3; break;
            case 0xF5: do_sbc(read(addr_zpx())); cycles = 4; break;
            case 0xED: do_sbc(read(addr_abs())); cycles = 4; break;
            case 0xFD: do_sbc(read(addr_absx())); cycles = 4; break;
            case 0xF9: do_sbc(read(addr_absy())); cycles = 4; break;
            case 0xE1: do_sbc(read(addr_indx())); cycles = 6; break;
            case 0xF1: do_sbc(read(addr_indy())); cycles = 5; break;

            // CMP
            case 0xC9: do_cmp(a, fetch_byte()); cycles = 2; break;
            case 0xC5: do_cmp(a, read(addr_zp())); cycles = 3; break;
            case 0xD5: do_cmp(a, read(addr_zpx())); cycles = 4; break;
            case 0xCD: do_cmp(a, read(addr_abs())); cycles = 4; break;
            case 0xDD: do_cmp(a, read(addr_absx())); cycles = 4; break;
            case 0xD9: do_cmp(a, read(addr_absy())); cycles = 4; break;
            case 0xC1: do_cmp(a, read(addr_indx())); cycles = 6; break;
            case 0xD1: do_cmp(a, read(addr_indy())); cycles = 5; break;

            // CPX
            case 0xE0: do_cmp(x, fetch_byte()); cycles = 2; break;
            case 0xE4: do_cmp(x, read(addr_zp())); cycles = 3; break;
            case 0xEC: do_cmp(x, read(addr_abs())); cycles = 4; break;

            // CPY
            case 0xC0: do_cmp(y, fetch_byte()); cycles = 2; break;
            case 0xC4: do_cmp(y, read(addr_zp())); cycles = 3; break;
            case 0xCC: do_cmp(y, read(addr_abs())); cycles = 4; break;

            // AND
            case 0x29: a &= fetch_byte(); set_nz(a); cycles = 2; break;
            case 0x25: a &= read(addr_zp()); set_nz(a); cycles = 3; break;
            case 0x35: a &= read(addr_zpx()); set_nz(a); cycles = 4; break;
            case 0x2D: a &= read(addr_abs()); set_nz(a); cycles = 4; break;
            case 0x3D: a &= read(addr_absx()); set_nz(a); cycles = 4; break;
            case 0x39: a &= read(addr_absy()); set_nz(a); cycles = 4; break;
            case 0x21: a &= read(addr_indx()); set_nz(a); cycles = 6; break;
            case 0x31: a &= read(addr_indy()); set_nz(a); cycles = 5; break;

            // ORA
            case 0x09: a |= fetch_byte(); set_nz(a); cycles = 2; break;
            case 0x05: a |= read(addr_zp()); set_nz(a); cycles = 3; break;
            case 0x15: a |= read(addr_zpx()); set_nz(a); cycles = 4; break;
            case 0x0D: a |= read(addr_abs()); set_nz(a); cycles = 4; break;
            case 0x1D: a |= read(addr_absx()); set_nz(a); cycles = 4; break;
            case 0x19: a |= read(addr_absy()); set_nz(a); cycles = 4; break;
            case 0x01: a |= read(addr_indx()); set_nz(a); cycles = 6; break;
            case 0x11: a |= read(addr_indy()); set_nz(a); cycles = 5; break;

            // EOR
            case 0x49: a ^= fetch_byte(); set_nz(a); cycles = 2; break;
            case 0x45: a ^= read(addr_zp()); set_nz(a); cycles = 3; break;
            case 0x55: a ^= read(addr_zpx()); set_nz(a); cycles = 4; break;
            case 0x4D: a ^= read(addr_abs()); set_nz(a); cycles = 4; break;
            case 0x5D: a ^= read(addr_absx()); set_nz(a); cycles = 4; break;
            case 0x59: a ^= read(addr_absy()); set_nz(a); cycles = 4; break;
            case 0x41: a ^= read(addr_indx()); set_nz(a); cycles = 6; break;
            case 0x51: a ^= read(addr_indy()); set_nz(a); cycles = 5; break;

            // BIT
            case 0x24: {
                uint8_t m = read(addr_zp());
                p = (p & ~(FLAG_N | FLAG_V | FLAG_Z)) | (m & (FLAG_N | FLAG_V)) | ((a & m) == 0 ? FLAG_Z : 0);
                cycles = 3;
                break;
            }
            case 0x2C: {
                uint8_t m = read(addr_abs());
                p = (p & ~(FLAG_N | FLAG_V | FLAG_Z)) | (m & (FLAG_N | FLAG_V)) | ((a & m) == 0 ? FLAG_Z : 0);
                cycles = 4;
                break;
            }

            // Increments & Decrements
            case 0xE8: x++; set_nz(x); cycles = 2; break; // INX
            case 0xC8: y++; set_nz(y); cycles = 2; break; // INY
            case 0xCA: x--; set_nz(x); cycles = 2; break; // DEX
            case 0x88: y--; set_nz(y); cycles = 2; break; // DEY
            case 0xE6: { uint16_t ad = addr_zp(); uint8_t v = read(ad) + 1; write(ad, v); set_nz(v); cycles = 5; break; }
            case 0xF6: { uint16_t ad = addr_zpx(); uint8_t v = read(ad) + 1; write(ad, v); set_nz(v); cycles = 6; break; }
            case 0xEE: { uint16_t ad = addr_abs(); uint8_t v = read(ad) + 1; write(ad, v); set_nz(v); cycles = 6; break; }
            case 0xFE: { uint16_t ad = addr_absx(); uint8_t v = read(ad) + 1; write(ad, v); set_nz(v); cycles = 7; break; }
            case 0xC6: { uint16_t ad = addr_zp(); uint8_t v = read(ad) - 1; write(ad, v); set_nz(v); cycles = 5; break; }
            case 0xD6: { uint16_t ad = addr_zpx(); uint8_t v = read(ad) - 1; write(ad, v); set_nz(v); cycles = 6; break; }
            case 0xCE: { uint16_t ad = addr_abs(); uint8_t v = read(ad) - 1; write(ad, v); set_nz(v); cycles = 6; break; }
            case 0xDE: { uint16_t ad = addr_absx(); uint8_t v = read(ad) - 1; write(ad, v); set_nz(v); cycles = 7; break; }

            // Shifts: ASL
            case 0x0A: set_c(a & 0x80); a <<= 1; set_nz(a); cycles = 2; break;
            case 0x06: { uint16_t ad = addr_zp(); uint8_t v = read(ad); set_c(v & 0x80); v <<= 1; write(ad, v); set_nz(v); cycles = 5; break; }
            case 0x16: { uint16_t ad = addr_zpx(); uint8_t v = read(ad); set_c(v & 0x80); v <<= 1; write(ad, v); set_nz(v); cycles = 6; break; }
            case 0x0E: { uint16_t ad = addr_abs(); uint8_t v = read(ad); set_c(v & 0x80); v <<= 1; write(ad, v); set_nz(v); cycles = 6; break; }
            case 0x1E: { uint16_t ad = addr_absx(); uint8_t v = read(ad); set_c(v & 0x80); v <<= 1; write(ad, v); set_nz(v); cycles = 7; break; }

            // Shifts: LSR
            case 0x4A: set_c(a & 0x01); a >>= 1; set_nz(a); cycles = 2; break;
            case 0x46: { uint16_t ad = addr_zp(); uint8_t v = read(ad); set_c(v & 0x01); v >>= 1; write(ad, v); set_nz(v); cycles = 5; break; }
            case 0x56: { uint16_t ad = addr_zpx(); uint8_t v = read(ad); set_c(v & 0x01); v >>= 1; write(ad, v); set_nz(v); cycles = 6; break; }
            case 0x4E: { uint16_t ad = addr_abs(); uint8_t v = read(ad); set_c(v & 0x01); v >>= 1; write(ad, v); set_nz(v); cycles = 6; break; }
            case 0x5E: { uint16_t ad = addr_absx(); uint8_t v = read(ad); set_c(v & 0x01); v >>= 1; write(ad, v); set_nz(v); cycles = 7; break; }

            // Shifts: ROL
            case 0x2A: { bool c = (p & FLAG_C); set_c(a & 0x80); a = (a << 1) | (c ? 1 : 0); set_nz(a); cycles = 2; break; }
            case 0x26: { uint16_t ad = addr_zp(); uint8_t v = read(ad); bool c = (p & FLAG_C); set_c(v & 0x80); v = (v << 1) | (c ? 1 : 0); write(ad, v); set_nz(v); cycles = 5; break; }
            case 0x36: { uint16_t ad = addr_zpx(); uint8_t v = read(ad); bool c = (p & FLAG_C); set_c(v & 0x80); v = (v << 1) | (c ? 1 : 0); write(ad, v); set_nz(v); cycles = 6; break; }
            case 0x2E: { uint16_t ad = addr_abs(); uint8_t v = read(ad); bool c = (p & FLAG_C); set_c(v & 0x80); v = (v << 1) | (c ? 1 : 0); write(ad, v); set_nz(v); cycles = 6; break; }
            case 0x3E: { uint16_t ad = addr_absx(); uint8_t v = read(ad); bool c = (p & FLAG_C); set_c(v & 0x80); v = (v << 1) | (c ? 1 : 0); write(ad, v); set_nz(v); cycles = 7; break; }

            // Shifts: ROR
            case 0x6A: { bool c = (p & FLAG_C); set_c(a & 0x01); a = (a >> 1) | (c ? 0x80 : 0); set_nz(a); cycles = 2; break; }
            case 0x66: { uint16_t ad = addr_zp(); uint8_t v = read(ad); bool c = (p & FLAG_C); set_c(v & 0x01); v = (v >> 1) | (c ? 0x80 : 0); write(ad, v); set_nz(v); cycles = 5; break; }
            case 0x76: { uint16_t ad = addr_zpx(); uint8_t v = read(ad); bool c = (p & FLAG_C); set_c(v & 0x01); v = (v >> 1) | (c ? 0x80 : 0); write(ad, v); set_nz(v); cycles = 6; break; }
            case 0x6E: { uint16_t ad = addr_abs(); uint8_t v = read(ad); bool c = (p & FLAG_C); set_c(v & 0x01); v = (v >> 1) | (c ? 0x80 : 0); write(ad, v); set_nz(v); cycles = 6; break; }
            case 0x7E: { uint16_t ad = addr_absx(); uint8_t v = read(ad); bool c = (p & FLAG_C); set_c(v & 0x01); v = (v >> 1) | (c ? 0x80 : 0); write(ad, v); set_nz(v); cycles = 7; break; }

            // Branches
            case 0x90: do_branch(!(p & FLAG_C)); break; // BCC
            case 0xB0: do_branch(p & FLAG_C); break;  // BCS
            case 0xF0: do_branch(p & FLAG_Z); break;  // BEQ
            case 0xD0: do_branch(!(p & FLAG_Z)); break; // BNE
            case 0x30: do_branch(p & FLAG_N); break;  // BMI
            case 0x10: do_branch(!(p & FLAG_N)); break; // BPL
            case 0x50: do_branch(!(p & FLAG_V)); break; // BVC
            case 0x70: do_branch(p & FLAG_V); break;  // BVS
            case 0x80: do_branch(true); break;        // BRA (65C02)

            // Flags
            case 0x18: p &= ~FLAG_C; cycles = 2; break; // CLC
            case 0x38: p |= FLAG_C;  cycles = 2; break; // SEC
            case 0x58: p &= ~FLAG_I; cycles = 2; break; // CLI
            case 0x78: p |= FLAG_I;  cycles = 2; break; // SEI
            case 0xB8: p &= ~FLAG_V; cycles = 2; break; // CLV
            case 0xD8: p &= ~FLAG_D; cycles = 2; break; // CLD
            case 0xF8: p |= FLAG_D;  cycles = 2; break; // SED

            // JMP
            case 0x4C: pc = fetch_word(); cycles = 3; break;
            case 0x6C: {
                uint16_t ptr = fetch_word();
                // 6502 indirect page wrap bug: if ptr is at page boundary, wraps around within page
                uint8_t lo = read(ptr);
                uint8_t hi = read((ptr & 0xFF00) | static_cast<uint8_t>((ptr & 0xFF) + 1));
                pc = (static_cast<uint16_t>(hi) << 8) | lo;
                cycles = 5;
                break;
            }

            // JSR & RTS
            case 0x20: {
                uint16_t target = fetch_word();
                uint16_t ret = pc - 1;
                push(static_cast<uint8_t>(ret >> 8));
                push(static_cast<uint8_t>(ret & 0xFF));
                pc = target;
                cycles = 6;
                break;
            }
            case 0x60: {
                uint8_t lo = pull();
                uint8_t hi = pull();
                pc = ((static_cast<uint16_t>(hi) << 8) | lo) + 1;
                cycles = 6;
                break;
            }

            // RTI
            case 0x40: {
                p = (pull() & ~FLAG_B) | FLAG_U;
                uint8_t lo = pull();
                uint8_t hi = pull();
                pc = (static_cast<uint16_t>(hi) << 8) | lo;
                cycles = 6;
                break;
            }

            // BRK
            case 0x00: {
                pc++; // BRK skips 1 padding byte
                push(static_cast<uint8_t>(pc >> 8));
                push(static_cast<uint8_t>(pc & 0xFF));
                push(p | FLAG_B | FLAG_U);
                p |= FLAG_I;
                pc = read16(0xFFFE);
                cycles = 7;
                break;
            }

            // ---- Remaining W65C02S instructions --------------------------------------
            // (zp) indirect, no index
            case 0x12: a |= read(addr_zpind()); set_nz(a); cycles = 5; break;          // ORA (zp)
            case 0x32: a &= read(addr_zpind()); set_nz(a); cycles = 5; break;          // AND (zp)
            case 0x52: a ^= read(addr_zpind()); set_nz(a); cycles = 5; break;          // EOR (zp)
            case 0x72: do_adc(read(addr_zpind())); cycles = 5; break;                  // ADC (zp)
            case 0x92: write(addr_zpind(), a); cycles = 5; break;                      // STA (zp)
            case 0xB2: a = read(addr_zpind()); set_nz(a); cycles = 5; break;           // LDA (zp)
            case 0xD2: do_cmp(a, read(addr_zpind())); cycles = 5; break;               // CMP (zp)
            case 0xF2: do_sbc(read(addr_zpind())); cycles = 5; break;                  // SBC (zp)
            // INC A / DEC A
            case 0x1A: a = static_cast<uint8_t>(a + 1); set_nz(a); cycles = 2; break;
            case 0x3A: a = static_cast<uint8_t>(a - 1); set_nz(a); cycles = 2; break;
            // BIT: immediate only affects Z; the others also copy bits 7/6 into N/V
            case 0x89: { uint8_t m = fetch_byte(); if (a & m) p &= ~FLAG_Z; else p |= FLAG_Z; cycles = 2; break; }
            case 0x34: { uint8_t m = read(addr_zpx()); do_bit(m); cycles = 4; break; }
            case 0x3C: { uint8_t m = read(addr_absx()); do_bit(m); cycles = 4; break; }
            // TSB / TRB: Z from A AND memory, then set / reset those bits in memory
            case 0x04: { uint16_t ad = addr_zp();  uint8_t m = read(ad); if (a & m) p &= ~FLAG_Z; else p |= FLAG_Z; write(ad, m | a);  cycles = 5; break; }
            case 0x0C: { uint16_t ad = addr_abs(); uint8_t m = read(ad); if (a & m) p &= ~FLAG_Z; else p |= FLAG_Z; write(ad, m | a);  cycles = 6; break; }
            case 0x14: { uint16_t ad = addr_zp();  uint8_t m = read(ad); if (a & m) p &= ~FLAG_Z; else p |= FLAG_Z; write(ad, m & ~a); cycles = 5; break; }
            case 0x1C: { uint16_t ad = addr_abs(); uint8_t m = read(ad); if (a & m) p &= ~FLAG_Z; else p |= FLAG_Z; write(ad, m & ~a); cycles = 6; break; }
            // JMP (abs,X)
            case 0x7C: { uint16_t base = static_cast<uint16_t>(fetch_word() + x);
                         pc = static_cast<uint16_t>(read(base) | (read(static_cast<uint16_t>(base + 1)) << 8)); cycles = 6; break; }
            // RMBn / SMBn zp  (x7: RMB for x = 0-7, SMB for x = 8-F)
            case 0x07: case 0x17: case 0x27: case 0x37: case 0x47: case 0x57: case 0x67: case 0x77:
            case 0x87: case 0x97: case 0xA7: case 0xB7: case 0xC7: case 0xD7: case 0xE7: case 0xF7: {
                uint16_t ad = addr_zp(); uint8_t bit = static_cast<uint8_t>(1u << ((op >> 4) & 7));
                uint8_t m = read(ad); write(ad, (op & 0x80) ? (m | bit) : (m & ~bit)); cycles = 5; break; }
            // BBRn / BBSn zp,rel  (xF: BBR for x = 0-7, BBS for x = 8-F)
            case 0x0F: case 0x1F: case 0x2F: case 0x3F: case 0x4F: case 0x5F: case 0x6F: case 0x7F:
            case 0x8F: case 0x9F: case 0xAF: case 0xBF: case 0xCF: case 0xDF: case 0xEF: case 0xFF: {
                uint8_t m = read(addr_zp()); bool set = (m >> ((op >> 4) & 7)) & 1;
                cycles = 5; do_branch((op & 0x80) ? set : !set); break; }
            // WAI: wait for an interrupt; STP: stop until reset
            case 0xCB: if (!irq_line && !nmi_pending) pc--; cycles = 3; break;
            case 0xDB: pc--; cycles = 3; break;

            default:
                cycles = 2;
                break;
        }

        return cycles;
    }
};
