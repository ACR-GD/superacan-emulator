#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include "sound/m6502.h"
#include "state_io.h"

class APU {
public:
    APU();
    void reset();

    // 68000 interface
    uint8_t read_sound_ram(uint16_t addr) const { return sound_ram[addr]; }
    void write_sound_ram(uint16_t addr, uint8_t val) { sound_ram[addr] = val; }

    uint8_t get_mailbox_a() const { return shadow_040a; }
    uint8_t get_mailbox_c() const { return shadow_040c; }
    uint8_t get_mailbox_d() const { return shadow_040d; }

    void write_cmd(); // Triggered by 68k write to E9000A

    // A 68000 write into sound RAM through $E80000. Writes to $0404/$0405 also fill the byte
    // latches the 65C02 reads there (empty latches read $CD) and raise their interrupts
    // ($08 / $04), after the MiSTer core's um6619_cpu.
    void host_write(uint16_t addr, uint8_t val);
    void set_cpu_reset(bool running); // 68k E9001C bit 0

    // Step sound subsystem for N sound CPU cycles (3.579545 MHz)
    void step(int cycles);

    // Audio buffer (interleaved signed 16-bit stereo at 44,744 Hz)
    const std::vector<int16_t>& get_audio_buffer() const { return audio_buffer; }
    void clear_audio_buffer() { audio_buffer.clear(); }

    void serialize(StateIO& s) {
        s.pod(sound_ram);
        // 6502 registers (its std::function bus callbacks are left untouched)
        s.pod(cpu.a); s.pod(cpu.x); s.pod(cpu.y); s.pod(cpu.sp); s.pod(cpu.p); s.pod(cpu.pc);
        s.pod(cpu.irq_line); s.pod(cpu.nmi_line); s.pod(cpu.nmi_pending);
        s.pod(cpu_running); s.pod(irq6_asserted);
        s.pod(shift0); s.pod(shift1); s.pod(latch0); s.pod(latch1);
        s.pod(latch0_full); s.pod(latch1_full); s.pod(shift_ctrl);
        s.pod(shadow_040a); s.pod(shadow_040c); s.pod(shadow_040d);
        s.pod(irq_enable); s.pod(irq_status); s.pod(snd_reg_addr); s.pod(regs);
        s.pod(reg_11); s.pod(reg_12); s.pod(reg_14); s.pod(reg_16);
        s.pod(timer_running); s.pod(timer_phase); s.pod(timer_period); s.pod(timer_left);
        s.pod(irq_timer); s.pod(irq_dma);
        s.pod(ch); s.pod(cycle_accumulator);
        if (s.loading) audio_buffer.clear();
    }

    // Sound style (a user preference, not part of save states):
    //   false = Accurate: hardware volume envelopes (attack/decay/sustain/release), as the
    //           MiSTer FPGA core models the UM6619;
    //   true  = Sustained: notes hold full volume until keyed off, like MAME, which does not
    //           implement the envelope registers. Louder and fuller, less accurate.
    void set_sustained(bool on) { sustained = on; }

    // The console's built-in 65C02 program (internal_6502_1.bin + internal_6502_2.bin, 16 KB)
    // is copied into sound RAM $0000-$3FFF on every reset, as on the hardware. Most games
    // upload their own sound driver; a few (e.g. Speedy Dragon) rely on the built-in one.
    void set_sound_bios(const std::vector<uint8_t>& data) { sound_bios = data; }
    bool has_sound_bios() const { return !sound_bios.empty(); }
    bool is_sustained() const { return sustained; }

    // Debug views (tools only)
    uint16_t debug_pc() const { return cpu.pc; }
    bool debug_running() const { return cpu_running; }
    uint8_t debug_irq_enable() const { return irq_enable; }
    uint8_t debug_irq_status() const { return irq_status; }

    bool has_irq6() const { return irq6_asserted; }
    void clear_irq6() { irq6_asserted = false; }

private:
    std::array<uint8_t, 0x10000> sound_ram;
    M6502 cpu;
    bool cpu_running = false;
    bool sustained = false;
    std::vector<uint8_t> sound_bios;
    bool irq6_asserted = false;

    // 6502 I/O page ($0400-$04FF)
    uint8_t shift0 = 0;
    uint8_t shift1 = 0;
    uint8_t latch0 = 0;
    uint8_t latch1 = 0;
    bool latch0_full = false;
    bool latch1_full = false;
    uint8_t shift_ctrl = 0;
    uint8_t shadow_040a = 0;
    uint8_t shadow_040c = 0;
    uint8_t shadow_040d = 0;
    uint8_t irq_enable = 0;
    uint8_t irq_status = 0;

    // Sampler indirect registers ($0420 / $0422)
    uint8_t snd_reg_addr = 0;
    uint8_t regs[256];

    // Sampler timer
    uint8_t reg_11 = 0;
    uint8_t reg_12 = 0;
    uint8_t reg_14 = 0;
    uint8_t reg_16 = 0;
    bool timer_running = false;
    bool timer_phase = false;
    int32_t timer_period = 0;
    int32_t timer_left = 0;
    bool irq_timer = false;
    bool irq_dma = false;

    // Per-channel state (16 channels)
    struct Channel {
        uint16_t pitch = 0;
        uint32_t incr = 0;
        uint16_t start = 0;
        uint16_t length = 0;
        uint16_t current = 0;
        uint16_t end_addr = 0;
        uint16_t fraction = 0;
        uint8_t  dma_flag = 0;
        uint8_t  vol_l = 0;
        uint8_t  vol_r = 0;
        bool     one_shot = false;
        bool     active = false;
        bool     releasing = false;
        bool     attacking = false;
        bool     decaying = false;
        uint32_t release_level = 0;
    } ch[16];

    int cycle_accumulator = 0;
    std::vector<int16_t> audio_buffer;

    uint8_t cpu_read(uint16_t addr);
    void cpu_write(uint16_t addr, uint8_t val);
    void sampler_write(uint8_t reg, uint8_t val);
    void update_irq();
    void generate_sample();

    static inline int16_t sat16(int32_t v) {
        if (v > 32767) return 32767;
        if (v < -32768) return -32768;
        return static_cast<int16_t>(v);
    }
};
