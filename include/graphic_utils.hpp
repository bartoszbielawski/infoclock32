#ifndef GRAPHIC_UTILS_HPP
#define GRAPHIC_UTILS_HPP

#pragma once

#include <Adafruit_GFX.h>
#include <LMDS.hpp>

void copyCanvasToDisplay(GFXcanvas1 &canvas, uint16_t canvasOffset, LMDS &display, uint16_t displayOffset = 0);
void scollMessage(std::string message, LMDS& display, int speed = 100);

void wipeDisplayLeftToRight(LMDS& display, int speed = 50);
void scollOutDisplayRight(LMDS& display, int speed = 50);

#endif // GRAPHIC_UTILS_HPP