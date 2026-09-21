#include "display.hpp"

#include <LMDS.hpp>

#include <Arduino.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace host_display
{
namespace
{
LMDS* display_ = nullptr;
bool ansi_ = true;

std::string snapshot()
{
    const int w = display_->getSegments() * 8;
    std::string frame;
    frame.reserve(16 * 64 + 64);
    frame += "\n-- infoclock32 host prototype -- 64x8 --\n";
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < w; x++)
            frame += display_->getPixel(x, y) ? '#' : ' ';
        frame += '\n';
    }
    if (ansi_) frame = "\033[2J\033[H" + frame;
    return frame;
}

void rendererLoop()
{
    std::string last;
    while (true)
    {
        if (display_)
        {
            std::string frame = snapshot();
            if (frame != last)
            {
                // one locked write per frame so log lines don't split the
                // picture; frames go to stdout, logs (Serial) to stderr
                fwrite(frame.data(), 1, frame.size(), stdout);
                fflush(stdout);
                last = std::move(frame);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}
}  // namespace

void init(LMDS* display, bool ansi)
{
    display_ = display;
    ansi_ = ansi;
    std::thread(rendererLoop).detach();
}
}  // namespace host_display
