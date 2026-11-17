// CPU trace harness used to diagnose the battle reset (see README). Needs Musashi built with
// -DM68K_INSTRUCTION_HOOK=M68K_OPT_ON, e.g.:
//   cc -O2 -I deps/musashi -DM68K_INSTRUCTION_HOOK=M68K_OPT_ON -c deps/musashi/m68kcpu.c deps/musashi/m68kops.c

#include "emu.h"
#include "savestate.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
extern "C" {
#include "m68k.h"
}
static uint32_t ring_ip[4096], ring_pc[4096]; static unsigned rpos = 0;
static uint32_t vec[64]; static bool armed = false, hit = false; static uint32_t hit_pc = 0, init_pc = 0;
static uint32_t WATCH = 0xffe226;
static uint32_t prevpc = 0, prev2 = 0; static int frame_no = 0;
static void hook(unsigned pc) {
    if (!armed || hit) { return; }
    uint32_t pp = prevpc; prev2 = prevpc; prevpc = pc;
    static uint32_t dsrc = 0, ddst = 0; static bool in_lz = false;
    if (pc == 0x1650) { ddst = m68k_get_reg(0,M68K_REG_A0); dsrc = m68k_get_reg(0,M68K_REG_A1); in_lz = true;
        fprintf(stderr, "frame %d: LZSS src=%06x dst=%06x  (caller IP=%06x RS0=%06x RS1=%06x)\n", frame_no, dsrc, ddst,
                m68k_get_reg(0,M68K_REG_A4), m68k_read_memory_32(m68k_get_reg(0,M68K_REG_SP)), m68k_read_memory_32(m68k_get_reg(0,M68K_REG_SP)+4));
        if (getenv("RSDUMP")) { uint32_t sp = m68k_get_reg(0,M68K_REG_SP); fprintf(stderr, "    RS:"); for (int i=0;i<16;i++) fprintf(stderr," %06x", m68k_read_memory_32(sp+4*i)); fprintf(stderr,"\n");
            auto var=[](uint32_t w){ return m68k_read_memory_32(m68k_read_memory_32(w+8)); };
            fprintf(stderr, "    2e38(base)=%06x 2dfc(off)=%06x 2e3c=%06x 2e00=%06x\n", var(0x2e38), var(0x2dfc), var(0x2e3c), var(0x2e00)); } }
    if (in_lz && m68k_read_memory_16(pc) == 0x2A5C) { in_lz = false;
        fprintf(stderr, "           ...ended: wrote %06x..%06x (%u bytes), consumed src up to %06x\n", ddst, m68k_get_reg(0,M68K_REG_A0), m68k_get_reg(0,M68K_REG_A0)-ddst, m68k_get_reg(0,M68K_REG_A1)); }
    static uint32_t last = 0xdeadbeef;
    uint32_t cur = m68k_read_memory_32(WATCH);
    if (cur != last) {
        fprintf(stderr, "frame %d: [%06x] %08x -> %08x  written by pc %06x (op %04x)  IP=%06x A5=%06x\n",
                frame_no, WATCH, last, cur, pp, m68k_read_memory_16(pp), m68k_get_reg(0,M68K_REG_A4), m68k_get_reg(0,M68K_REG_A5));
        last = cur;
    }
    unsigned op = m68k_read_memory_16(pc);
    if (op == 0x2A5C) { ring_ip[rpos & 4095] = m68k_get_reg(nullptr, M68K_REG_A4); ring_pc[rpos & 4095] = pc; rpos++; }
    if (pc == init_pc) { hit = true; hit_pc = pc; fprintf(stderr, "RESET ENTRY from pc %06x\n", pp); return; }
    if (false) {
        fprintf(stderr, "frame %d: exception handler %06x entered; faulting pc %06x opcode %04x  A4=%06x A5=%06x d0=%08x\n",
                frame_no, pc, pp, m68k_read_memory_16(pp), m68k_get_reg(0,M68K_REG_A4), m68k_get_reg(0,M68K_REG_A5), m68k_get_reg(0,M68K_REG_D0));
    }
}
int main(int argc, char** argv) {
    SuperAcan e; if (!e.load_roms(argv[1], argv[2])) return 1;
    if (!SaveState::load(e, argv[3])) return 1;
    for (int v = 0; v < 64; v++) vec[v] = m68k_read_memory_32(v * 4);
    init_pc = vec[1];
    fprintf(stderr, "reset pc %06x, vectors: bus %06x addr %06x illg %06x zdiv %06x chk %06x priv %06x lineA %06x lineF %06x\n",
            vec[1], vec[2], vec[3], vec[4], vec[5], vec[6], vec[8], vec[10], vec[11]);
    m68k_set_instr_hook_callback(hook);
    armed = true;
    struct H { int f, len; JoypadButton b; } holds[] = {{10,4,BTN_B},{120,4,BTN_LEFT},{170,4,BTN_B}};
    for (int f = 0; f < 600 && !hit; f++) {
        for (auto b : {BTN_B, BTN_LEFT}) { bool on = false; for (auto& h : holds) if (h.b == b && f >= h.f && f < h.f + h.len) on = true; e.get_joypad().set_button(0, b, on); }
        frame_no = f; e.get_apu().clear_audio_buffer(); e.run_frame();
        if (hit) fprintf(stderr, "HIT at frame %d pc=%06x\n", f, hit_pc);
    }
    if (!hit) { fprintf(stderr, "no reset/exception detected\n"); return 0; }
    fprintf(stderr, "SR=%04x SP=%08x A4(IP)=%08x A6(DSP)=%08x A5=%08x\n", m68k_get_reg(0,M68K_REG_SR), m68k_get_reg(0,M68K_REG_SP), m68k_get_reg(0,M68K_REG_A4), m68k_get_reg(0,M68K_REG_A6), m68k_get_reg(0,M68K_REG_A5));
    // dump last 120 IPs, with the cell value (word executed)
    unsigned n = rpos < 120 ? rpos : 120;
    for (unsigned i = rpos - n; i < rpos; i++) {
        uint32_t ip = ring_ip[i & 4095];
        fprintf(stdout, "%06x %08x\n", ip, m68k_read_memory_32(ip));
    }
    // data stack top
    uint32_t dsp = m68k_get_reg(0, M68K_REG_A6);
    fprintf(stderr, "DS:"); for (int i = 0; i < 8; i++) fprintf(stderr, " %08x", m68k_read_memory_32(dsp + i*4)); fprintf(stderr, "\n");
    uint32_t rsp = m68k_get_reg(0, M68K_REG_SP);
    fprintf(stderr, "RS:"); for (int i = 0; i < 12; i++) fprintf(stderr, " %08x", m68k_read_memory_32(rsp + i*4)); fprintf(stderr, "\n");
}
