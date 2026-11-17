#include "emu.h"
#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" {
#include "m68k.h"

unsigned int m68k_read_memory_8(unsigned int address) {
    return g_bus ? g_bus->read8(address) : 0xFF;
}

unsigned int m68k_read_memory_16(unsigned int address) {
    return g_bus ? g_bus->read16(address) : 0xFFFF;
}

unsigned int m68k_read_memory_32(unsigned int address) {
    return g_bus ? g_bus->read32(address) : 0xFFFFFFFF;
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
    if (g_bus) g_bus->write8(address, static_cast<uint8_t>(value));
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
    if (g_bus) g_bus->write16(address, static_cast<uint16_t>(value));
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
    if (g_bus) g_bus->write32(address, value);
}

// Interrupt encoder (after the MiSTer core's um6619_irq): each source holds its level until
// the 68000 acknowledges that level; the CPU sees the highest pending one.
static uint8_t g_irq_pending = 0;   // bit n = level n pending

static void update_irq_line() {
    int level = 0;
    for (int l = 7; l >= 1; --l) if (g_irq_pending & (1 << l)) { level = l; break; }
    m68k_set_irq(level);
}

void raise_irq(int level) { g_irq_pending |= (1 << level); update_irq_line(); }
void clear_irq(int level) { g_irq_pending &= ~(1 << level); update_irq_line(); }

int int_ack_callback(int int_level) {
    g_irq_pending &= ~(1 << int_level);
    if (int_level == 3 && g_bus) g_bus->ack_frc_irq();
    update_irq_line();
    return M68K_INT_ACK_AUTOVECTOR;
}

} // extern "C"

SuperAcan::SuperAcan() : running(false) {
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(int_ack_callback);
}

SuperAcan::~SuperAcan() {
}

void SuperAcan::restore_cpu_hooks() {
    // A saved Musashi context contains raw pointers that are only valid in the process that
    // wrote it; rebuild them from known-good values.
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(int_ack_callback);
    m68k_set_bkpt_ack_callback(nullptr);
    m68k_set_reset_instr_callback(nullptr);
    m68k_set_pc_changed_callback(nullptr);
    m68k_set_tas_instr_callback(nullptr);
    m68k_set_illg_instr_callback(nullptr);
    m68k_set_trap_instr_callback(nullptr);
    m68k_set_fc_callback(nullptr);
    m68k_set_instr_hook_callback(nullptr);
}

bool SuperAcan::load_roms(const std::string& prg_path, const std::string& dat_path) {
    if (!bus.load_prg_rom(prg_path)) {
        return false;
    }
    if (!dat_path.empty()) {
        bus.load_dat_rom(dat_path);
    }
    reset();
    running = true;
    return true;
}

bool SuperAcan::load_bios(const std::string& dir) {
    std::vector<uint8_t> all;
    for (const char* name : {"internal_6502_1.bin", "internal_6502_2.bin"}) {
        std::ifstream f(dir + "/" + name, std::ios::binary);
        if (!f) return false;
        std::vector<uint8_t> part((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (part.size() != 0x2000) return false;
        all.insert(all.end(), part.begin(), part.end());
    }
    bus.get_apu().set_sound_bios(all);
    auto slurp = [&](const char* name) {
        std::ifstream f(dir + "/" + name, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    std::vector<uint8_t> boot = slurp("internal_68k.bin"), key = slurp("umc6650.bin");
    if (boot.size() == 0x1000 && key.size() == 0x10) bus.set_boot_rom(boot, key);
    else std::cout << "[SuperAcan] internal_68k.bin / umc6650.bin missing: direct boot\n";
    std::cout << "[SuperAcan] Console sound program loaded from " << dir << "\n";
    return true;
}

void SuperAcan::reset() {
    g_irq_pending = 0;
    bus.reset();
    sound_cycle_rem = 0;
    m68k_pulse_reset();
    std::cout << "[SuperAcan] CPU Reset completed. PC: 0x" << std::hex << m68k_get_reg(NULL, M68K_REG_PC)
              << " SP: 0x" << m68k_get_reg(NULL, M68K_REG_SP) << std::dec << "\n";
}

void SuperAcan::run_frame() {
    constexpr int CYCLES_PER_LINE = 683; // 178,977 / 262 ≈ 683

    for (int line = 0; line < PPU::TOTAL_LINES; ++line) {
        bus.get_ppu().set_vpos(line);

        // Raster comparators ($F0000A on / $F0000C off, bit 15 = enable): interrupt level 5
        // goes up when the beam reaches the "on" line and down at the "off" line, held until
        // acknowledged (after the MiSTer core's um6618). Used for raster effects
        // (e.g. Sango Fighter's window transitions).
        {
            uint16_t ron = bus.get_ppu().read_reg(0x0A), roff = bus.get_ppu().read_reg(0x0C);
            if ((ron & 0x8000) && (line & 0xFF) == (ron & 0xFF)) raise_irq(5);
            if ((roff & 0x8000) && (line & 0xFF) == (roff & 0xFF)) clear_irq(5);
        }

        // Execute 68000 CPU instructions for this scanline
        m68k_execute(CYCLES_PER_LINE);

        // Free running counter (interrupt level 3)
        bus.step_frc(CYCLES_PER_LINE);
        if (bus.frc_irq_pending()) raise_irq(3);

        // Step 6502 Sound CPU and APU (1/3 of 68k clock: exact fractional cycle tracking)
        int sound_cycles = (CYCLES_PER_LINE + sound_cycle_rem) / 3;
        sound_cycle_rem = (CYCLES_PER_LINE + sound_cycle_rem) % 3;
        bus.get_apu().step(sound_cycles);

        // Check if 6502 wrote to host mailbox (sets 68k Level 6 IRQ)
        if (bus.get_apu().has_irq6()) {
            bus.get_apu().clear_irq6();
            raise_irq(6);
        }

        if (line < PPU::SCREEN_HEIGHT) {
            // Active display: render scanline
            bus.get_ppu().render_scanline(line);

            // Scanline IRQ (Level 4) if enabled
            if (bus.get_irq_mask() & 0x10) {
                raise_irq(4);
            }
        } else if (line == PPU::SCREEN_HEIGHT) {
            // VBlank begins at line 240
            bus.get_ppu().set_vblank(true);

            // VBlank IRQ (Level 7) if enabled
            if (bus.get_irq_mask() & 0x80) {
                raise_irq(7);
            }
            bus.frc_vblank();
            if (bus.frc_irq_pending()) raise_irq(3);
        } else if (line == PPU::TOTAL_LINES - 1) {
            // VBlank ends at last line
            bus.get_ppu().set_vblank(false);
            // Unacknowledged display interrupts end with the frame (the previous behaviour,
            // which Super Dragon Force's timing was validated against).
            clear_irq(7);
            clear_irq(4);
        }
    }
}

uint32_t SuperAcan::get_pc() const {
    return m68k_get_reg(NULL, M68K_REG_PC);
}

uint32_t SuperAcan::get_sp() const {
    return m68k_get_reg(NULL, M68K_REG_SP);
}

