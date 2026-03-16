#ifndef GRAPHIC_UTILS_HPP
#define GRAPHIC_UTILS_HPP

#pragma once

#include <Adafruit_GFX.h>
#include <LMDS.hpp>

void copyCanvasToDisplay(GFXcanvas1 &canvas, uint16_t canvasOffset, LMDS &display, uint16_t displayOffset = 0);

void scrollMessage(std::string message, LMDS& display, int speed = 100, int step = 1);


void wipeDisplayLeftToRight(LMDS& display, int speed = 50);
void scrollOutDisplayRight(LMDS& display, int speed = 50);
void scrollOutDisplayLeft(LMDS& display, int speed = 50);
void scrollOutDisplayUp(LMDS& display, int speed = 80);
void scrollOutDisplayDown(LMDS& display, int speed = 80);
void wipeTopToBottom(LMDS& display, int speed = 50);
void wipeCurtain(LMDS& display, int speed = 50);
void wipeVenetianBlinds(LMDS& display, int speed = 150);
void wipeStaticNoise(LMDS& display, int speed = 5);
void wipeSpiralInward(LMDS& display, int speed = 1);
void wipeDiagonal(LMDS& display, int speed = 15);
void wipeSplitToCenter(LMDS& display, int speed = 40);
void wipeColumnsFromCenter(LMDS& display, int speed = 40);

#endif // GRAPHIC_UTILS_HPP