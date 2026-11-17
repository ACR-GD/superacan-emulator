#pragma once
#include <string>
#include <map>
#include <cstdint>
#include "input/joypad.h"

// Configuration and settings management
class Config {
public:
    struct KeyBinding {
        int sdl_key;
        JoypadButton button;
    };

    // Video settings
    int window_scale = 3;
    bool vsync = true;
    bool fullscreen = false;
    int frameskip = 0; // 0 = no skip, 1 = render every other frame, etc.

    // Audio settings
    bool audio_enabled = true;
    int audio_sample_rate = 44744;
    int audio_buffer_size = 1024;

    // Debug settings
    bool debug_mode = false;
    bool show_fps = true;
    bool show_debug_info = false;

    // Keybindings (SDL key -> Button)
    std::map<int, JoypadButton> keybindings;

    Config();
    
    bool load_from_file(const std::string& path);
    bool save_to_file(const std::string& path);
    void set_default_keybindings();
};
