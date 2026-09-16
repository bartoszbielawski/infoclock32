#pragma once
// MAX7219 frame decoder + terminal renderer for the host prototype.
//
// The SPI shim forwards every 16-bit transfer here; the GPIO shim reports CS
// edges. A latch (CS rising) applies the captured words to the framebuffer —
// mirroring LEDMatrixDriver::_displayRow: word j in send order belongs to
// controller j, register 1..8 = row. When the last row (register 8) latches,
// the framebuffer is complete and gets rendered (throttled to ~4 fps).

namespace host_display
{
// Register hooks on the global SPI shim and GPIO shim.
// ansi: clear-screen live view; plain: static text frames.
void init(int segments, int csPin, bool ansi);
}
