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

// Emscripten/JavaScript stub for EventManager.
// Input events under Emscripten are driven from JavaScript, so these
// methods are no-ops; the static instance is still required to satisfy
// callers that use EventManager::get_instance().

#include <core/hostevents.h>

EventManager* EventManager::event_manager;

void EventManager::set_keyboard_locale(uint32_t keyboard_id) {
    this->kbd_locale = keyboard_id;
}

void EventManager::poll_events() {
    this->_post_signal.emit();
}

void EventManager::post_keyboard_state_events() {
}
