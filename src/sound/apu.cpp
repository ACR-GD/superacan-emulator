#include <algorithm>
#include "sound/apu.h"
#include <cstring>
#include <iostream>

APU::APU() {
    cpu.read_fn = [this](uint16_t addr) -> uint8_t {
        return this->cpu_read(addr);
    };
    cpu.write_fn = [this](uint16_t addr, uint8_t val) {
        this->cpu_write(addr, val);
    };
    reset();
}

void APU::reset() {
    sound_ram.fill(0);
    if (!sound_bios.empty()) {
        std::copy_n(sound_bios.begin(), std::min<size_t>(sound_bios.size(), 0x4000), sound_ram.begin());
    }
    std::memset(regs, 0, sizeof(regs));
    cpu_running = false;
    irq6_asserted = false;

    shift0 = shift1 = 0;
    latch0 = latch1 = 0;
    latch0_full = latch1_full = false;
    shift_ctrl = 0;
    shadow_040a = shadow_040c = shadow_040d = 0;
    irq_enable = 0;
    irq_status = 0;

    snd_reg_addr = 0;
    reg_11 = reg_12 = reg_14 = reg_16 = 0;
    timer_running = false;
    timer_phase = false;
    timer_period = 0;
    timer_left = 0;
    irq_timer = false;
    irq_dma = false;

    for (int i = 0; i < 16; ++i) {
        ch[i] = Channel();
    }
    cycle_accumulator = 0;
    audio_buffer.clear();
}

void APU::host_write(uint16_t addr, uint8_t val) {
    sound_ram[addr] = val;
    if (addr == 0x0404) { latch0 = val; latch0_full = true; irq_status |= 0x08; update_irq(); }
    else if (addr == 0x0405) { latch1 = val; latch1_full = true; irq_status |= 0x04; update_irq(); }
}

void APU::write_cmd() {
    // 68000 wrote to E9000A: sets IRQ bit 5
    if (irq_enable & 0x20) {
        irq_status |= 0x20;
        update_irq();
    }
}

void APU::set_cpu_reset(bool running) {
    if (running && !cpu_running) {
        cpu_running = true;
        cpu.reset();
        std::cout << "[APU] 6502 Sound CPU reset released. PC: 0x"
                  << std::hex << cpu.pc << std::dec << "\n";
    } else if (!running && cpu_running) {
        cpu_running = false;
        std::cout << "[APU] 6502 Sound CPU halted.\n";
    }
}

void APU::update_irq() {
    uint8_t live = (irq_timer ? 0x80 : 0x00) |
                   (irq_dma   ? 0x40 : 0x00) |
                   (irq_status & 0x3F);
    bool asserted = (live & irq_enable) != 0;
    cpu.set_irq(asserted);
}

uint8_t APU::cpu_read(uint16_t addr) {
    if ((addr & 0xFF00) == 0x0400) {
        uint8_t reg = addr & 0xFF;
        switch (reg) {
            case 0x02: return shift0;
            case 0x03: return shift1;
            case 0x04: {
                irq_status &= ~0x08;
                update_irq();
                uint8_t res = latch0_full ? latch0 : 0xCD;
                latch0_full = false;
                return res;
            }
            case 0x05: {
                irq_status &= ~0x04;
                update_irq();
                uint8_t res = latch1_full ? latch1 : 0xCD;
                latch1_full = false;
                return res;
            }
            case 0x06: return 0;
            case 0x09:
                irq_status &= ~0x10;
                update_irq();
                return 0;
            case 0x0A:
                irq_status &= ~0x20;
                update_irq();
                return shadow_040a;
            case 0x10: return irq_enable;
            case 0x11:
                return (irq_timer ? 0x80 : 0x00) |
                       (irq_dma   ? 0x40 : 0x00) |
                       (irq_status & 0x3F);
            case 0x12: return 0; // clears NMI
            case 0x20: return 0; // status
            case 0x22: {
                if (snd_reg_addr == 0x14) {
                    irq_timer = false;
                    update_irq();
                    return (reg_14 & 0xBF) | (timer_phase ? 0x40 : 0x00);
                }
                if (snd_reg_addr == 0x16) {
                    irq_dma = false;
                    update_irq();
                    bool dma_busy = false;
                    for (int i = 0; i < 16; ++i) {
                        if (ch[i].active && ch[i].dma_flag != 0) {
                            dma_busy = true;
                            break;
                        }
                    }
                    return (reg_16 & ~0x40) | (dma_busy ? 0x40 : 0x00);
                }
                return regs[snd_reg_addr];
            }
            default:
                return 0;
        }
    }
    return sound_ram[addr];
}

void APU::cpu_write(uint16_t addr, uint8_t val) {
    if ((addr & 0xFF00) == 0x0400) {
        uint8_t reg = addr & 0xFF;
        switch (reg) {
            case 0x07: shift_ctrl = val; break;
            case 0x0A:
                shadow_040a = val;
                irq6_asserted = true;
                break;
            case 0x0C: shadow_040c = val; break;
            case 0x0D: shadow_040d = val; break;
            case 0x10:
                irq_enable = val;
                update_irq();
                break;
            case 0x20:
                snd_reg_addr = val;
                break;
            case 0x22:
                sampler_write(snd_reg_addr, val);
                break;
            default:
                break;
        }
        return;
    }
    sound_ram[addr] = val;
}

void APU::sampler_write(uint8_t reg, uint8_t val) {
    regs[reg] = val;

    switch (reg) {
        case 0x11: reg_11 = val; break;
        case 0x12: reg_12 = val; break;
        case 0x14: {
            reg_14 = val;
            if ((val & 0xC0) == 0) {
                timer_running = false;
                irq_timer = false;
                update_irq();
            } else if (!timer_running) {
                timer_running = true;
                uint16_t timer_n = (static_cast<uint16_t>(reg_12) << 8) | reg_11;
                timer_period = 5 * (0x10000 - timer_n);
                timer_left = timer_period;
            }
            break;
        }
        case 0x16: reg_16 = val; break;
        case 0x17: {
            int c = val & 0x0F;
            if ((val & 0xF0) != 0) {
                // Key On
                bool ctrl1 = (regs[0x40 + c] & 0x02) != 0;
                bool dma = (ch[c].dma_flag != 0);
                uint8_t attack = regs[0xA0 + c] & 0x1F;

                if (!ctrl1 || dma || attack == 31) {
                    ch[c].release_level = 0xFFFFFFFF;
                } else if (!ch[c].active) {
                    ch[c].release_level = 0;
                }
                ch[c].attacking = (attack != 31);
                ch[c].decaying = true;
                ch[c].releasing = false;
                ch[c].current = (ch[c].start << 6) & 0xFFFF;
                ch[c].end_addr = (ch[c].current + ch[c].length) & 0xFFFF;
                ch[c].fraction = 0;
                ch[c].active = true;
            } else {
                // Key Off
                bool ctrl1 = (regs[0x40 + c] & 0x02) != 0;
                bool dma = (ch[c].dma_flag != 0);
                if (ctrl1 && !dma && !sustained) {
                    ch[c].releasing = true;
                } else {
                    ch[c].active = false;
                }
            }
            break;
        }
        default:
            break;
    }

    uint8_t top_nibble = reg >> 4;
    int c = reg & 0x0F;

    switch (top_nibble) {
        case 0x2: // Pitch low
            ch[c].pitch = (ch[c].pitch & 0xFF00) | val;
            ch[c].incr = static_cast<uint32_t>(ch[c].pitch) << 6;
            break;
        case 0x3: // Pitch high
            ch[c].pitch = (ch[c].pitch & 0x00FF) | (static_cast<uint16_t>(val) << 8);
            ch[c].incr = static_cast<uint32_t>(ch[c].pitch) << 6;
            break;
        case 0x5: // Length & Loop
            ch[c].length = 0x40 << ((val >> 1) & 7);
            ch[c].one_shot = (val & 1) != 0;
            break;
        case 0x6: // Start high
            ch[c].start = (ch[c].start & 0x00FF) | (static_cast<uint16_t>(val) << 8);
            break;
        case 0x7: // Start low
            ch[c].start = (ch[c].start & 0xFF00) | val;
            break;
        case 0x9: // DMA flag
            ch[c].dma_flag = val;
            break;
        case 0xE: // Volume
            ch[c].vol_l = val & 0xF0;
            ch[c].vol_r = (val & 0x0F) << 4;
            break;
        default:
            break;
    }
}

void APU::step(int cycles) {
    int elapsed = 0;
    while (elapsed < cycles) {
        int cpu_cycles = 1;
        if (cpu_running) {
            cpu_cycles = cpu.step();
        }

        // Advance timer
        if (timer_running) {
            timer_left -= cpu_cycles;
            if (timer_left <= 0) {
                if (timer_period > 0) {
                    timer_left += timer_period;
                } else {
                    timer_left = 0x10000;
                }
                timer_phase = !timer_phase;
                irq_timer = true;
                update_irq();
            }
        }

        // Advance sample clock
        cycle_accumulator += cpu_cycles;
        while (cycle_accumulator >= 80) {
            cycle_accumulator -= 80;
            generate_sample();
        }

        elapsed += cpu_cycles;
    }
}

void APU::generate_sample() {
    int32_t acc_l = 0;
    int32_t acc_r = 0;

    for (int c = 0; c < 16; ++c) {
        if (!ch[c].active) continue;

        uint8_t sample = sound_ram[ch[c].current];
        if (sample == 0 && ch[c].dma_flag != 0 && ch[c].one_shot) {
            ch[c].active = false;
            irq_dma = true;
            update_irq();
            continue;
        }

        int32_t pcm = static_cast<int32_t>(sample) - 128;
        int32_t eff_l = ch[c].vol_l;
        int32_t eff_r = ch[c].vol_r;

        bool ctrl1 = (regs[0x40 + c] & 0x02) != 0;
        bool envelope = ctrl1 && (ch[c].dma_flag == 0) && !sustained;
        if (sustained && ch[c].releasing) {   // switched style mid-note: end the release now
            ch[c].releasing = false;
            ch[c].active = false;
            continue;
        }

        if (ch[c].releasing || envelope) {
            uint32_t rel = ch[c].release_level >> 24;
            eff_l = ((eff_l * rel) >> 8) + (eff_l >> 8);
            eff_r = ((eff_r * rel) >> 8) + (eff_r >> 8);
        }

        acc_l += pcm * eff_l;
        acc_r += pcm * eff_r;

        // Step Envelope
        if (ch[c].releasing) {
            uint8_t release_rate = regs[0xD0 + c] & 0x0F;
            uint32_t release_step = (release_rate == 0) ? 0xFFFFFFFF : (857u << release_rate);
            if (ch[c].release_level <= release_step) {
                ch[c].release_level = 0;
                ch[c].active = false;
            } else {
                ch[c].release_level -= release_step;
            }
        } else if (envelope) {
            uint8_t attack_rate = regs[0xA0 + c] & 0x1F;
            uint8_t decay_rate  = regs[0xB0 + c] & 0x1F;
            uint8_t sustain_rate = regs[0xC0 + c] & 0x1F;
            uint32_t sustain_level = 0xFFFFFFFF - ((regs[0xD0 + c] & 0xF0) << 24);

            if (ch[c].attacking) {
                if (attack_rate != 0) {
                    uint32_t gap = ~ch[c].release_level;
                    int shift = 18 - (attack_rate >> 1);
                    uint32_t step = gap >> (shift > 0 ? shift : 0);
                    if (attack_rate == 31 || gap <= 0x01FFFFFF) {
                        ch[c].release_level = 0xFFFFFFFF;
                        ch[c].attacking = false;
                    } else {
                        ch[c].release_level += step;
                    }
                }
            } else if (ch[c].decaying) {
                if (decay_rate != 0) {
                    uint32_t gap = (ch[c].release_level > sustain_level) ? (ch[c].release_level - sustain_level) : 0;
                    int shift = 20 - (decay_rate >> 1);
                    uint32_t step = gap >> (shift > 0 ? shift : 0);
                    if (gap <= 0x00FFFFFF) {
                        ch[c].release_level = sustain_level;
                        ch[c].decaying = false;
                    } else {
                        ch[c].release_level -= step;
                    }
                }
            } else if (sustain_rate != 0) {
                uint32_t sustain_step = 857u << (sustain_rate >> 1);
                if (ch[c].release_level <= sustain_step) {
                    ch[c].release_level = 0;
                    ch[c].active = false;
                } else {
                    ch[c].release_level -= sustain_step;
                }
            }
        }

        // Step Phase
        uint32_t step_sum = ch[c].fraction + ch[c].incr;
        ch[c].fraction = step_sum & 0xFFFF;
        ch[c].current = (ch[c].current + (step_sum >> 16)) & 0xFFFF;

        if (ch[c].current >= ch[c].end_addr) {
            if (ch[c].dma_flag != 0) {
                irq_dma = true;
                update_irq();
                ch[c].current = (ch[c].start << 6) & 0xFFFF;
                ch[c].end_addr = (ch[c].current + ch[c].length) & 0xFFFF;
            } else if (ch[c].one_shot) {
                ch[c].active = false;
            } else if (ch[c].length != 0) {
                ch[c].current = (ch[c].current - ch[c].length) & 0xFFFF;
            }
        }
    }

    int16_t out_l = sat16(acc_l >> 4);
    int16_t out_r = sat16(acc_r >> 4);
    audio_buffer.push_back(out_l);
    audio_buffer.push_back(out_r);
}
