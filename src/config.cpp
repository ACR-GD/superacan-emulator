#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <SDL.h>

Config::Config() {
    set_default_keybindings();
}

void Config::set_default_keybindings() {
    keybindings.clear();
    
    // D-Pad
    keybindings[SDLK_UP] = BTN_UP;
    keybindings[SDLK_DOWN] = BTN_DOWN;
    keybindings[SDLK_LEFT] = BTN_LEFT;
    keybindings[SDLK_RIGHT] = BTN_RIGHT;
    keybindings[SDLK_w] = BTN_UP;
    keybindings[SDLK_s] = BTN_DOWN;
    keybindings[SDLK_a] = BTN_LEFT;
    keybindings[SDLK_d] = BTN_RIGHT;
    
    // Face buttons
    keybindings[SDLK_z] = BTN_B;
    keybindings[SDLK_j] = BTN_B;
    keybindings[SDLK_SPACE] = BTN_B;
    keybindings[SDLK_x] = BTN_Y;
    keybindings[SDLK_k] = BTN_Y;
    keybindings[SDLK_BACKSPACE] = BTN_Y;
    keybindings[SDLK_c] = BTN_X;
    keybindings[SDLK_u] = BTN_X;
    
    // Shoulder buttons
    keybindings[SDLK_q] = BTN_L;
    keybindings[SDLK_o] = BTN_L;
    keybindings[SDLK_v] = BTN_R;
    keybindings[SDLK_i] = BTN_R;
    
    // Start/Select
    keybindings[SDLK_RETURN] = BTN_START;
    keybindings[SDLK_KP_ENTER] = BTN_START;
    keybindings[SDLK_RSHIFT] = BTN_SELECT;
    keybindings[SDLK_LSHIFT] = BTN_SELECT;
    keybindings[SDLK_TAB] = BTN_SELECT;
}

bool Config::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cout << "[Config] No config file found at " << path << ", using defaults.\n";
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream iss(line);
        std::string key, eq, value;
        if (iss >> key >> eq >> value && eq == "=") {
            if (key == "window_scale") window_scale = std::stoi(value);
            else if (key == "vsync") vsync = (value == "true" || value == "1");
            else if (key == "fullscreen") fullscreen = (value == "true" || value == "1");
            else if (key == "frameskip") frameskip = std::stoi(value);
            else if (key == "audio_enabled") audio_enabled = (value == "true" || value == "1");
            else if (key == "show_fps") show_fps = (value == "true" || value == "1");
            else if (key == "debug_mode") debug_mode = (value == "true" || value == "1");
        }
    }
    
    std::cout << "[Config] Loaded configuration from " << path << "\n";
    return true;
}

bool Config::save_to_file(const std::string& path) {
    std::ofstream file(path);
    if (!file) {
        std::cerr << "[Config] Failed to save config to " << path << "\n";
        return false;
    }

    file << "# Super A'Can Emulator Configuration\n\n";
    file << "# Video Settings\n";
    file << "window_scale = " << window_scale << "\n";
    file << "vsync = " << (vsync ? "true" : "false") << "\n";
    file << "fullscreen = " << (fullscreen ? "true" : "false") << "\n";
    file << "frameskip = " << frameskip << "\n\n";
    
    file << "# Audio Settings\n";
    file << "audio_enabled = " << (audio_enabled ? "true" : "false") << "\n\n";
    
    file << "# Display Settings\n";
    file << "show_fps = " << (show_fps ? "true" : "false") << "\n";
    file << "debug_mode = " << (debug_mode ? "true" : "false") << "\n";

    std::cout << "[Config] Saved configuration to " << path << "\n";
    return true;
}
