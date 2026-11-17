#include "savestate.h"
#include "emu.h"
#include "state_io.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>

extern "C" {
#include "m68k.h"
}

namespace {

struct Header {
    uint32_t magic;
    uint32_t version;
    uint64_t timestamp;
    uint64_t rom_fingerprint;
    uint32_t cpu_context_size;
    uint32_t payload_size;
};

uint64_t fingerprint(const std::vector<uint8_t>& rom) {
    uint64_t h = 1469598103934665603ULL; // FNV-1a 64
    for (uint8_t b : rom) { h ^= b; h *= 1099511628211ULL; }
    return h ^ rom.size();
}

} // namespace

std::string SaveState::get_quick_save_path(int slot) {
    return "quicksave_" + std::to_string(slot) + ".sav";
}

bool SaveState::save(const SuperAcan& emu_c, const std::string& path) {
    // serialize() is symmetric (non-const); saving does not modify the machine.
    SuperAcan& emu = const_cast<SuperAcan&>(emu_c);

    StateIO io;
    std::vector<uint8_t> cpu(m68k_context_size());
    m68k_get_context(cpu.data());
    io.bytes(cpu.data(), cpu.size());
    emu.get_bus().serialize(io);
    io.pod(emu.sound_cycle_remainder());

    Header h{};
    h.magic = MAGIC;
    h.version = VERSION;
    h.timestamp = static_cast<uint64_t>(std::time(nullptr));
    h.rom_fingerprint = fingerprint(emu.get_bus().get_prg_rom());
    h.cpu_context_size = static_cast<uint32_t>(cpu.size());
    h.payload_size = static_cast<uint32_t>(io.buf.size());

    // Write to a temp file then rename, so a crash never leaves a truncated state behind.
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { std::cerr << "[SaveState] Cannot write " << path << "\n"; return false; }
        f.write(reinterpret_cast<const char*>(&h), sizeof(h));
        f.write(reinterpret_cast<const char*>(io.buf.data()), static_cast<std::streamsize>(io.buf.size()));
        if (!f) { std::cerr << "[SaveState] Write failed for " << path << "\n"; return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::cerr << "[SaveState] Could not finalize " << path << "\n";
        return false;
    }
    std::cout << "[SaveState] Saved " << path << " (" << io.buf.size() / 1024 << " KB)\n";
    return true;
}

bool SaveState::load(SuperAcan& emu, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "[SaveState] Cannot open " << path << "\n"; return false; }

    Header h{};
    f.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!f || h.magic != MAGIC) { std::cerr << "[SaveState] Not a save state: " << path << "\n"; return false; }
    if (h.version != VERSION) { std::cerr << "[SaveState] Unsupported state version " << h.version << "\n"; return false; }
    if (h.rom_fingerprint != fingerprint(emu.get_bus().get_prg_rom())) {
        // Debug/testing escape hatch (e.g. reusing a state after rebuilding the translation).
        if (!std::getenv("SACAN_IGNORE_ROM_MISMATCH")) {
            std::cerr << "[SaveState] State belongs to a different ROM\n";
            return false;
        }
        std::cerr << "[SaveState] Warning: loading a state from a different ROM build\n";
    }
    if (h.cpu_context_size != m68k_context_size()) {
        std::cerr << "[SaveState] CPU core mismatch\n";
        return false;
    }

    StateIO io;
    io.loading = true;
    io.buf.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (io.buf.size() != h.payload_size) { std::cerr << "[SaveState] Truncated state\n"; return false; }

    // Reject states whose layout doesn't match this build before touching the live machine.
    StateIO probe;
    std::vector<uint8_t> cpu_probe(h.cpu_context_size);
    probe.bytes(cpu_probe.data(), cpu_probe.size());
    emu.get_bus().serialize(probe);
    probe.pod(emu.sound_cycle_remainder());
    // States written before battery-SRAM support lack the 32 KB SRAM block, which sits at the
    // end of the bus data (just before the trailing 4-byte sound-cycle remainder). Splice in
    // the current SRAM so those states still load.
    const size_t sram_size = emu.get_bus().get_sram().size();
    if (io.buf.size() + sram_size == probe.buf.size()) {
        const auto& sr = emu.get_bus().get_sram();
        io.buf.insert(io.buf.end() - sizeof(int), sr.begin(), sr.end());
        std::cerr << "[SaveState] Older state without save RAM; keeping current save RAM\n";
    }
    if (probe.buf.size() != io.buf.size()) { std::cerr << "[SaveState] State layout mismatch\n"; return false; }

    std::vector<uint8_t> cpu(h.cpu_context_size);
    io.bytes(cpu.data(), cpu.size());
    emu.get_bus().serialize(io);
    io.pod(emu.sound_cycle_remainder());
    if (!io.ok) { std::cerr << "[SaveState] Corrupt state\n"; return false; }

    m68k_set_context(cpu.data());
    emu.restore_cpu_hooks();

    std::cout << "[SaveState] Loaded " << path << "\n";
    return true;
}
