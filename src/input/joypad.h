#pragma once
#include <cstdint>

enum JoypadButton : uint16_t {
    BTN_R      = (1 << 4),
    BTN_L      = (1 << 5),
    BTN_Y      = (1 << 6),
    BTN_X      = (1 << 7),
    BTN_RIGHT  = (1 << 8),
    BTN_LEFT   = (1 << 9),
    BTN_DOWN   = (1 << 10),
    BTN_UP     = (1 << 11),
    BTN_SELECT = (1 << 12),
    BTN_START  = (1 << 13),
    BTN_B      = (1 << 14),
    BTN_A      = (1 << 15),
};

class Joypad {
public:
    Joypad() : p1_buttons(0), p2_buttons(0) {}

    void set_button(int player, JoypadButton button, bool pressed) {
        uint16_t& state = (player == 0) ? p1_buttons : p2_buttons;
        if (pressed) {
            state |= static_cast<uint16_t>(button);
        } else {
            state &= ~static_cast<uint16_t>(button);
        }
    }

    uint16_t get_buttons(int player) const {
        return (player == 0) ? p1_buttons : p2_buttons;
    }

    template <class IO> void serialize(IO& s) { s.pod(p1_buttons); s.pod(p2_buttons); }

    void reset() {
        p1_buttons = 0;
        p2_buttons = 0;
    }

private:
    uint16_t p1_buttons;
    uint16_t p2_buttons;
};
