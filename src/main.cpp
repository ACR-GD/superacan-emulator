#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <SDL.h>
#include "m68k.h"
#include "emu.h"
#include "config.h"
#include "savestate.h"
#include <cstdio>

// Native macOS file picker (returns empty string if cancelled)
static std::string pick_rom_file() {
    FILE* p = popen("osascript -e 'try' -e 'POSIX path of (choose file with prompt \"Select a Super A'\\''Can ROM (.bin)\")' -e 'end try' 2>/dev/null", "r");
    if (!p) return "";
    char buf[4096] = {0};
    std::string out;
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return (!out.empty() && out[0] == '/') ? out : "";
}

int main(int argc, char* argv[]) {
    // Allow launching without ROM - will show GUI/ROM browser
    bool no_rom_provided = (argc < 2);

    std::string prg_path;
    std::string dat_path;
    int max_frames = 0;
    std::string screenshot_path;
    std::string wav_path;
    bool sound_sustained = false;   // --sound accurate|sustained
    std::string bios_dir;           // --bios <dir with internal_6502_1/2.bin>
    bool headless = false;

    struct ButtonPress {
        int start_frame;
        int end_frame;
        JoypadButton btn;
    };
    std::vector<ButtonPress> button_presses;

    struct ScheduledScreenshot {
        int frame;
        std::string path;
    };
    std::vector<ScheduledScreenshot> scheduled_screenshots;

    auto parse_btn = [](const std::string& name) -> JoypadButton {
        if (name == "start" || name == "START") return BTN_START;
        if (name == "select" || name == "SELECT") return BTN_SELECT;
        if (name == "a" || name == "A") return BTN_A;
        if (name == "b" || name == "B") return BTN_B;
        if (name == "x" || name == "X") return BTN_X;
        if (name == "y" || name == "Y") return BTN_Y;
        if (name == "up" || name == "UP") return BTN_UP;
        if (name == "down" || name == "DOWN") return BTN_DOWN;
        if (name == "left" || name == "LEFT") return BTN_LEFT;
        if (name == "right" || name == "RIGHT") return BTN_RIGHT;
        if (name == "l" || name == "L") return BTN_L;
        if (name == "r" || name == "R") return BTN_R;
        return BTN_START;
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            max_frames = std::stoi(argv[++i]);
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (arg == "--bios" && i + 1 < argc) {
            bios_dir = argv[++i];
        } else if (arg == "--sound" && i + 1 < argc) {
            sound_sustained = (std::string(argv[++i]) == "sustained");
        } else if (arg == "--wav" && i + 1 < argc) {
            wav_path = argv[++i];
        } else if (arg == "--screenshot-at" && i + 1 < argc) {
            std::string s = argv[++i];
            size_t colon = s.find(':');
            if (colon != std::string::npos) {
                int f = std::stoi(s.substr(0, colon));
                std::string path = s.substr(colon + 1);
                scheduled_screenshots.push_back({f, path});
            }
        } else if ((arg == "--press" || arg == "--button-press") && i + 1 < argc) {
            std::string s = argv[++i];
            size_t colon1 = s.find(':');
            size_t colon2 = s.rfind(':');
            if (colon1 != std::string::npos && colon2 != std::string::npos && colon1 != colon2) {
                int start = std::stoi(s.substr(0, colon1));
                int end = std::stoi(s.substr(colon1 + 1, colon2 - colon1 - 1));
                std::string btn_name = s.substr(colon2 + 1);
                button_presses.push_back({start, end, parse_btn(btn_name)});
            }
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg[0] != '-') {
            if (prg_path.empty()) {
                prg_path = arg;
            } else if (dat_path.empty()) {
                dat_path = arg;
            }
        }
    }

    if (max_frames > 0) {
        headless = true;
    }
    
    // If no ROM and no special modes, launch GUI-only mode
    bool gui_only_mode = no_rom_provided && !headless && max_frames == 0;
    if (prg_path.empty() && !headless) {
        prg_path = pick_rom_file();
        if (prg_path.empty()) {
            std::cerr << "[Main] No ROM selected. Exiting.\n";
            return 0;
        }
    }

    // If DAT path not specified, check if 08002 (Taiwan).bin or translated DAT exists
    // A ROM file over 2 MB carries its own data ROM (Sango Fighter, translation patches); don't override it.
    long prg_size = 0;
    if (FILE* pf = fopen(prg_path.c_str(), "rb")) { fseek(pf, 0, SEEK_END); prg_size = ftell(pf); fclose(pf); }
    if (dat_path.empty() && prg_size <= 0x200000) {
        std::vector<std::string> candidates = {
            "build/Super-Dragon-Force_DAT_EN.bin",
            "Super-Dragon-Force_Super-Acan_ZH-TW/08002 (Taiwan).bin",
            "08002 (Taiwan).bin",
            "../build/Super-Dragon-Force_DAT_EN.bin",
            "../Super-Dragon-Force_Super-Acan_ZH-TW/08002 (Taiwan).bin"
        };
        // Also search relative to the ROM's own folder (cwd is "/" when launched from Finder)
        std::string rom_dir = prg_path.substr(0, prg_path.find_last_of('/') + 1);
        if (!rom_dir.empty()) {
            candidates.insert(candidates.begin(), {
                rom_dir + "Super-Dragon-Force_DAT_EN.bin",
                rom_dir + "08002 (Taiwan).bin",
                rom_dir + "../Super-Dragon-Force_Super-Acan_ZH-TW/08002 (Taiwan).bin",
            });
        }
        for (const auto& c : candidates) {
            FILE* f = fopen(c.c_str(), "rb");
            if (f) {
                fclose(f);
                dat_path = c;
                std::cout << "[Main] Auto-detected DAT ROM: " << dat_path << "\n";
                break;
            }
        }
    }

    SuperAcan emu;
    
    if (!bios_dir.empty() && !emu.load_bios(bios_dir)) {
        std::cerr << "[Main] Warning: console system ROMs not found in " << bios_dir << "\n";
    }

    // Only load ROM if one was provided
    if (!prg_path.empty()) {
        
        if (!emu.load_roms(prg_path, dat_path)) {
            std::cerr << "[Main] Error: Could not load ROMs.\n";
            
            return 1;
        }
    } else if (headless) {
        std::cerr << "[Main] Error: Headless mode requires ROM argument.\n";
        std::cout << "Usage: " << argv[0] << " <prg_rom.bin> [options]\n";
        return 1;
    }
    emu.get_apu().set_sustained(sound_sustained);

    if (headless) {
        if (false) {
            std::cerr << "[Main] Error: No ROM loaded for headless mode.\n";
            return 1;
        }
        std::cout << "[Main] Running in headless mode for " << max_frames << " frames...\n";
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<int16_t> recorded_audio;
        for (int f = 0; f < max_frames; ++f) {
            // Apply scheduled button presses
            for (const auto& bp : button_presses) {
                if (f == bp.start_frame) {
                    emu.get_joypad().set_button(0, bp.btn, true);
                    std::cout << "[Main] Frame " << f << ": Pressed button " << bp.btn << "\n";
                }
                if (f == bp.end_frame) {
                    emu.get_joypad().set_button(0, bp.btn, false);
                    std::cout << "[Main] Frame " << f << ": Released button " << bp.btn << "\n";
                }
            }

            emu.get_apu().clear_audio_buffer();
            emu.run_frame();

            if (!wav_path.empty()) {
                const auto& ab = emu.get_apu().get_audio_buffer();
                recorded_audio.insert(recorded_audio.end(), ab.begin(), ab.end());
            }

            // Check scheduled screenshots
            for (const auto& ss : scheduled_screenshots) {
                if (f + 1 == ss.frame) {
                    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
                        (void*)emu.get_framebuffer(),
                        emu.get_active_width(), PPU::SCREEN_HEIGHT,
                        32, PPU::SCREEN_WIDTH * 4,
                        SDL_PIXELFORMAT_ARGB8888
                    );
                    if (surf) {
                        SDL_SaveBMP(surf, ss.path.c_str());
                        SDL_FreeSurface(surf);
                        std::cout << "[Main] Frame " << (f + 1) << ": Saved scheduled screenshot to " << ss.path << "\n";
                    }
                }
            }

            if ((f + 1) % 60 == 0 || f == max_frames - 1) {
                uint32_t pc = emu.get_pc();
                std::cout << "[Main] Frame " << (f + 1) << "/" << max_frames
                          << " | PC=0x" << std::hex << pc
                          << " SP=0x" << emu.get_sp()
                          << " A4(IP)=0x" << m68k_get_reg(NULL, M68K_REG_A4)
                          << " IRQMask=0x" << emu.get_bus().get_irq_mask()
                          << " SR=0x" << m68k_get_reg(NULL, M68K_REG_SR)
                          << std::dec << "\n";
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        std::cout << "[Main] Finished " << max_frames << " frames in " << diff.count() << "s ("
                  << (max_frames / diff.count()) << " FPS).\n";

        // Sound diagnostics
        APU& apu = emu.get_apu();
        std::cout << "[APU Diag] Total samples generated in last frame: "
                  << apu.get_audio_buffer().size() / 2 << "\n";
        int active_voices = 0;
        int16_t min_s = 0, max_s = 0;
        for (int16_t s : apu.get_audio_buffer()) {
            if (s < min_s) min_s = s;
            if (s > max_s) max_s = s;
        }
        std::cout << "[APU Diag] Sample range: [" << min_s << " .. " << max_s << "]\n";

        if (!wav_path.empty() && !recorded_audio.empty()) {
            FILE* wf = fopen(wav_path.c_str(), "wb");
            if (wf) {
                uint32_t data_bytes = recorded_audio.size() * sizeof(int16_t);
                uint32_t chunk_size = 36 + data_bytes;
                uint16_t num_channels = 2;
                uint32_t sample_rate = 44744;
                uint16_t bits_per_sample = 16;
                uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
                uint16_t block_align = num_channels * (bits_per_sample / 8);

                fwrite("RIFF", 1, 4, wf);
                fwrite(&chunk_size, 4, 1, wf);
                fwrite("WAVEfmt ", 1, 8, wf);
                uint32_t subchunk1_size = 16;
                uint16_t audio_format = 1; // PCM
                fwrite(&subchunk1_size, 4, 1, wf);
                fwrite(&audio_format, 2, 1, wf);
                fwrite(&num_channels, 2, 1, wf);
                fwrite(&sample_rate, 4, 1, wf);
                fwrite(&byte_rate, 4, 1, wf);
                fwrite(&block_align, 2, 1, wf);
                fwrite(&bits_per_sample, 2, 1, wf);
                fwrite("data", 1, 4, wf);
                fwrite(&data_bytes, 4, 1, wf);
                fwrite(recorded_audio.data(), sizeof(int16_t), recorded_audio.size(), wf);
                fclose(wf);
                std::cout << "[Main] Saved " << (recorded_audio.size() / 2) << " audio samples to "
                          << wav_path << " (" << data_bytes << " bytes)\n";
            }
        }

        if (!screenshot_path.empty()) {
            // Print PPU diagnostics
            PPU& ppu = emu.get_bus().get_ppu();
            std::cout << "[PPU Diag] video_flags=0x" << std::hex << ppu.read_reg(0x08)
                      << " pixel_mode=0x" << ppu.read_reg(0x1F0) << std::dec << "\n";

            int nonzero_pal = 0;
            for (int i = 0; i < 256; ++i) {
                if (ppu.read_palette(i * 2) != 0) nonzero_pal++;
            }
            std::cout << "[PPU Diag] Non-zero palette entries: " << nonzero_pal << "/256\n";

            int nonzero_vram = 0;
            for (int i = 0; i < 0x10000; ++i) {
                if (ppu.read_vram(i * 2) != 0) nonzero_vram++;
            }
            std::cout << "[PPU Diag] Non-zero VRAM words: " << nonzero_vram << "/65536\n";

            // Print ranges of non-zero VRAM
            int start_nz = -1;
            for (int i = 0; i < 0x10000; ++i) {
                if (ppu.read_vram(i * 2) != 0) {
                    if (start_nz == -1) start_nz = i;
                } else {
                    if (start_nz != -1) {
                        std::cout << "  VRAM [0x" << std::hex << start_nz << " .. 0x" << (i - 1) << "]\n";
                        start_nz = -1;
                    }
                }
            }
            if (start_nz != -1) {
                std::cout << "  VRAM [0x" << std::hex << start_nz << " .. 0xFFFF]\n";
            }
            std::cout << std::dec;

            for (int l = 0; l < 4; ++l) {
                uint32_t b = 0x100 + l * 0x20;
                std::cout << "  Layer " << l << ": flags=0x" << std::hex << ppu.read_reg(b + 0x00)
                          << " tile_mode=0x" << ppu.read_reg(b + 0x02)
                          << " sx=" << std::dec << (int16_t)ppu.read_reg(b + 0x04)
                          << " sy=" << (int16_t)ppu.read_reg(b + 0x06)
                          << " map_base=0x" << std::hex << ppu.read_reg(b + 0x08)
                          << " mode=0x" << ppu.read_reg(b + 0x0A)
                          << " lsx=0x" << ppu.read_reg(b + 0x0C)
                          << " lsel=0x" << ppu.read_reg(b + 0x0E)
                          << std::dec << "\n";
            }



            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
                (void*)emu.get_framebuffer(),
                emu.get_active_width(), PPU::SCREEN_HEIGHT,
                32, PPU::SCREEN_WIDTH * 4,
                SDL_PIXELFORMAT_ARGB8888
            );
            if (surf) {
                if (SDL_SaveBMP(surf, screenshot_path.c_str()) == 0) {
                    std::cout << "[Main] Saved screenshot to " << screenshot_path << "\n";
                } else {
                    std::cerr << "[Main] Failed to save screenshot: " << SDL_GetError() << "\n";
                }
                SDL_FreeSurface(surf);
            }
        }
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        std::cerr << "[Main] SDL_Init Error: " << SDL_GetError() << "\n";
        return 1;
    }

    const int SCALE = 3;
    const int WIN_WIDTH  = PPU::SCREEN_WIDTH * SCALE;  // 960
    const int WIN_HEIGHT = PPU::SCREEN_HEIGHT * SCALE; // 720

    SDL_Window* window = SDL_CreateWindow(
        "Super A'Can Emulator - Super Dragon Force",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_WIDTH, WIN_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );
    if (!window) {
        std::cerr << "[Main] SDL_CreateWindow Error: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED
    );
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, 0);
    }
    if (!renderer) {
        std::cerr << "[Main] SDL_CreateRenderer Error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_RenderSetLogicalSize(renderer, PPU::SCREEN_WIDTH, PPU::SCREEN_HEIGHT);

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        PPU::SCREEN_WIDTH, PPU::SCREEN_HEIGHT
    );
    if (!texture) {
        std::cerr << "[Main] SDL_CreateTexture Error: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_AudioDeviceID audio_dev = 0;
    SDL_AudioSpec wanted, have;
    SDL_zero(wanted);
    wanted.freq = 44744;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 1024;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &wanted, &have, 0);
    bool audio_started = false;
    if (audio_dev) {
        std::cout << "[Main] SDL Audio device opened: " << have.freq << " Hz, "
                  << static_cast<int>(have.channels) << " channels (pre-buffering enabled)\n";
    } else {
        std::cerr << "[Main] Warning: Failed to open audio device: " << SDL_GetError() << "\n";
    }

    // Create GUI manager

    
    std::cout << "====================================================================\n";
    std::cout << " Super A'Can Standalone Emulator - Super Dragon Force\n";
    std::cout << " Controls:\n";
    std::cout << "   D-Pad / Move:       Arrow Keys or WASD\n";
    std::cout << "   Confirm / Talk:     Z, Space, Enter, or J\n";
    std::cout << "   Cancel / Menu:      X, K, or Backspace\n";
    std::cout << "   Extra Buttons:      C (X), V (R), Q (L), Shift (Select)\n";
    std::cout << "   System:             F1 = Reset, ESC = Exit\n";
    std::cout << "   Save States:        F5/F6 = Save Slot 1/2, F9/F10 = Load Slot 1/2\n";
    std::cout << "====================================================================\n";

    bool quit = false;
    SDL_Event event;

    // High-resolution frame timing
    using clock = std::chrono::high_resolution_clock;
    const std::chrono::nanoseconds FRAME_TIME_NS(16666667); // 1.0 / 60.0 sec = 16.666 ms
    auto target_frame_end = clock::now() + FRAME_TIME_NS;
    auto last_fps_time = clock::now();
    int frame_count = 0;

    // Buffer cushion targets in bytes:
    // ~4 frames cushion (~3000 stereo samples = 12000 bytes)
    const uint32_t TARGET_QUEUED_BYTES = 3000 * 2 * sizeof(int16_t);
    const uint32_t MIN_QUEUED_BYTES    = 1500 * 2 * sizeof(int16_t);
    const uint32_t MAX_QUEUED_BYTES    = 6000 * 2 * sizeof(int16_t);

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            // Let GUI handle events first
                        
            
            if (event.type == SDL_QUIT) {
                quit = true;
            } else if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)) {
                bool pressed = (event.type == SDL_KEYDOWN);
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        if (pressed) quit = true;
                        break;
                    case SDLK_F1:
                        if (pressed) emu.reset();
                        break;
                    // F5-F8: Quick save to slots 1-4
                    case SDLK_F5:
                        if (pressed) {
                            std::string path = SaveState::get_quick_save_path(1);
                            if (SaveState::save(emu, path)) {
                                std::cout << "[Main] Saved state to slot 1\n";
                            }
                        }
                        break;
                    case SDLK_F6:
                        if (pressed) {
                            std::string path = SaveState::get_quick_save_path(2);
                            if (SaveState::save(emu, path)) {
                                std::cout << "[Main] Saved state to slot 2\n";
                            }
                        }
                        break;
                    // F9-F12: Quick load from slots 1-4
                    case SDLK_F9:
                        if (pressed) {
                            std::string path = SaveState::get_quick_save_path(1);
                            if (SaveState::load(emu, path)) {
                                std::cout << "[Main] Loaded state from slot 1\n";
                            }
                        }
                        break;
                    case SDLK_F10:
                        if (pressed) {
                            std::string path = SaveState::get_quick_save_path(2);
                            if (SaveState::load(emu, path)) {
                                std::cout << "[Main] Loaded state from slot 2\n";
                            }
                        }
                        break;

                    // D-Pad: Arrows or WASD
                    case SDLK_UP:
                    case SDLK_w:
                        emu.get_joypad().set_button(0, BTN_UP, pressed);
                        break;
                    case SDLK_DOWN:
                    case SDLK_s:
                        emu.get_joypad().set_button(0, BTN_DOWN, pressed);
                        break;
                    case SDLK_LEFT:
                    case SDLK_a:
                        emu.get_joypad().set_button(0, BTN_LEFT, pressed);
                        break;
                    case SDLK_RIGHT:
                    case SDLK_d:
                        emu.get_joypad().set_button(0, BTN_RIGHT, pressed);
                        break;

                    // Confirm / Talk / Advance (Primary Action): Z, Space, Enter, J
                    case SDLK_z:
                    case SDLK_j:
                    case SDLK_SPACE:
                        emu.get_joypad().set_button(0, BTN_B, pressed);
                        emu.get_joypad().set_button(0, BTN_A, pressed);
                        break;

                    // Enter / Return: Start + Confirm
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        emu.get_joypad().set_button(0, BTN_START, pressed);
                        emu.get_joypad().set_button(0, BTN_B, pressed);
                        break;

                    // Cancel / Menu (Secondary Action): X, K, Backspace
                    case SDLK_x:
                    case SDLK_k:
                    case SDLK_BACKSPACE:
                        emu.get_joypad().set_button(0, BTN_Y, pressed);
                        break;

                    // Sub-actions
                    case SDLK_c:
                    case SDLK_u:
                        emu.get_joypad().set_button(0, BTN_X, pressed);
                        break;
                    case SDLK_v:
                    case SDLK_i:
                        emu.get_joypad().set_button(0, BTN_R, pressed);
                        break;
                    case SDLK_q:
                    case SDLK_o:
                        emu.get_joypad().set_button(0, BTN_L, pressed);
                        break;

                    // Select
                    case SDLK_RSHIFT:
                    case SDLK_LSHIFT:
                    case SDLK_TAB:
                        emu.get_joypad().set_button(0, BTN_SELECT, pressed);
                        break;

                    default:
                        break;
                }
            }
        }

        // Run emulation for 1 full frame (262 scanlines, ~178,977 CPU cycles)
        emu.run_frame();

        // Audio output with dynamic cushion management
        if (audio_dev) {
            const auto& audio_buf = emu.get_apu().get_audio_buffer();
            if (!audio_buf.empty()) {
                uint32_t queued = SDL_GetQueuedAudioSize(audio_dev);

                if (!audio_started) {
                    // Accumulate cushion before unpausing
                    SDL_QueueAudio(audio_dev, audio_buf.data(), audio_buf.size() * sizeof(int16_t));
                    if (SDL_GetQueuedAudioSize(audio_dev) >= TARGET_QUEUED_BYTES) {
                        SDL_PauseAudioDevice(audio_dev, 0);
                        audio_started = true;
                    }
                } else {
                    if (queued < MIN_QUEUED_BYTES) {
                        // Queue running low: pad slightly to prevent underrun
                        std::vector<int16_t> padded_buf = audio_buf;
                        size_t pad = std::min(size_t(200), audio_buf.size());
                        padded_buf.insert(padded_buf.end(), audio_buf.end() - pad, audio_buf.end());
                        SDL_QueueAudio(audio_dev, padded_buf.data(), padded_buf.size() * sizeof(int16_t));
                    } else if (queued > MAX_QUEUED_BYTES) {
                        // Queue growing large: trim slightly to reduce latency
                        size_t trim = audio_buf.size() > 200 ? audio_buf.size() - 200 : audio_buf.size();
                        SDL_QueueAudio(audio_dev, audio_buf.data(), trim * sizeof(int16_t));
                    } else {
                        SDL_QueueAudio(audio_dev, audio_buf.data(), audio_buf.size() * sizeof(int16_t));
                    }
                }
            }
        }
        emu.get_apu().clear_audio_buffer();

        // Update video texture
        const uint32_t* fb = emu.get_framebuffer();
        SDL_UpdateTexture(texture, nullptr, fb, PPU::SCREEN_WIDTH * sizeof(uint32_t));

        SDL_RenderClear(renderer);
        // Only the active columns (256 in the H256 mode), stretched over the 4:3 picture.
        SDL_Rect active = {0, 0, emu.get_active_width(), PPU::SCREEN_HEIGHT};
        SDL_RenderCopy(renderer, texture, &active, nullptr);
        
        // Render GUI on top
        
        
        SDL_RenderPresent(renderer);

        frame_count++;
        auto now = clock::now();
        std::chrono::duration<double> elapsed = now - last_fps_time;
        if (elapsed.count() >= 1.0) {
            double fps = frame_count / elapsed.count();
            std::string title = "Super A'Can Emulator - Super Dragon Force [" + std::to_string(static_cast<int>(std::round(fps))) + " FPS]";
            SDL_SetWindowTitle(window, title.c_str());
            frame_count = 0;
            last_fps_time = now;
        }

        // Precision 60.0 FPS frame limiter (hybrid sleep + spin)
        now = clock::now();
        if (target_frame_end > now) {
            auto remaining = target_frame_end - now;
            if (remaining > std::chrono::milliseconds(2)) {
                auto sleep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining) - std::chrono::milliseconds(1);
                if (sleep_ms.count() > 0) {
                    SDL_Delay(static_cast<Uint32>(sleep_ms.count()));
                }
            }
            while (clock::now() < target_frame_end) {
                #if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
                #elif defined(__aarch64__)
                asm volatile("yield");
                #endif
            }
        }
        target_frame_end += FRAME_TIME_NS;
        if (clock::now() > target_frame_end + FRAME_TIME_NS) {
            target_frame_end = clock::now();
        }
    }

    if (audio_dev) {
        SDL_CloseAudioDevice(audio_dev);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    SDL_Quit();

    return 0;
}
