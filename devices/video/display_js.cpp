/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

(You may also contact divingkxt or powermax2286 on Discord)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Emscripten/JavaScript stub for the Display class.
// Rendering to the browser canvas is handled separately via JavaScript;
// these methods are no-ops that satisfy the linker.

#include <devices/video/display.h>

class Display::Impl {
};

Display::Display(): impl(std::make_unique<Impl>()) {
}

Display::~Display() {
}

bool Display::configure(int width, int height) {
    return true;
}

void Display::configure_dest() {
}

void Display::configure_texture() {
}

void Display::update_window_size() {
}

void Display::blank() {
}

void Display::update(std::function<void(uint8_t *dst_buf, int dst_pitch)> convert_fb_cb,
                     std::function<void(uint8_t *dst_buf, int dst_pitch)> cursor_ovl_cb,
                     bool draw_hw_cursor, int cursor_x, int cursor_y,
                     bool fb_known_to_be_changed) {
}

void Display::update_skipped() {
}

void Display::handle_events(const WindowEvent& wnd_event) {
}

void Display::setup_hw_cursor(std::function<void(uint8_t *dst_buf, int dst_pitch)> draw_hw_cursor,
                              int cursor_width, int cursor_height) {
}

void Display::update_window_title() {
}

void Display::toggle_mouse_grab() {
}

void Display::update_mouse_grab(bool will_be_grabbed) {
}
