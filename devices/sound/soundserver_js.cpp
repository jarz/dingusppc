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

// Emscripten/JavaScript stub for SoundServer.
// Audio output under Emscripten is handled via the Web Audio API through
// JavaScript; these methods are no-ops that satisfy the linker.

#include <devices/sound/soundserver.h>

class SoundServer::Impl {
};

SoundServer::SoundServer(): impl(std::make_unique<Impl>())
{
}

SoundServer::~SoundServer() = default;

int SoundServer::start()
{
    return 0;
}

void SoundServer::shutdown()
{
}

int SoundServer::open_out_stream(uint32_t sample_rate, DmaOutChannel *dma_ch)
{
    return 0;
}

int SoundServer::start_out_stream()
{
    return 0;
}

void SoundServer::close_out_stream()
{
}
