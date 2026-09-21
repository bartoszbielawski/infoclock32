#pragma once
// Terminal renderer for the host prototype.
//
// Reads the LMDS framebuffer directly (getPixel — the same ground truth as
// the driver's displayToSerial) from a background thread at ~4 fps. No SPI
// decoding involved: the framebuffer is live state, not double-buffered, so
// polling always shows the latest content.

class LMDS;

namespace host_display
{
// Start the renderer thread. ansi: clear-screen live view; static: plain
// text frames, printed only when the content changes.
void init(LMDS* display, bool ansi);
}
