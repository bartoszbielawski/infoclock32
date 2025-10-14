
#include <freertos/FreeRTOS.h>
#include "graphic_utils.hpp"


void copyCanvasToDisplay(GFXcanvas1 &canvas, uint16_t canvasOffset, LMDS &display, uint16_t displayOffset)
{
  int w = std::min(canvas.width(), display.width());
  int h = std::min(canvas.height(), display.height());

  for (int x = 0; x < w; x++)
  {
    uint8_t col = 0;
    for (int y = 0; y < h; y++)
    {
      bool v = canvas.getPixel(x + canvasOffset, y);
      display.setPixel(x + displayOffset, y, v);
    }
  }
}

void scollMessage(std::string message, LMDS& display, int speed)
{
  //assume you already have access to the display
  GFXcanvas1 canvas(512, 8);
  
  canvas.print(message.c_str());
  int msgLength = canvas.getCursorX();

  if (msgLength <= display.width())
  {
    //message fits on the display, no need to scroll, but center the message
    display.clear();
    int offset = (display.width() - msgLength) / 2;
    copyCanvasToDisplay(canvas, 0, display, offset);
    display.displayToSerial(Serial);
    vTaskDelay(10 * speed / portTICK_PERIOD_MS);
    return;
  }

  for (int offset = 0; offset < msgLength - display.width(); offset++)
  {   
    copyCanvasToDisplay(canvas, offset, display);
    display.displayToSerial(Serial);
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

void wipeDisplayLeftToRight(LMDS& display, int speed)
{
  //assume you already have access to the display
  for (int col = 0; col < display.getSegments() * 8; col++)
  {
    display.setColumn(col, 0x00);
    display.displayToSerial(Serial);
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

void scollOutDisplayRight(LMDS& display, int speed)
{
  //assume you already have access to the display
  for (int col = 0; col < display.getSegments() * 8; col++)
  {
    display.scroll(LMDS::scrollDirection::scrollRight);
    display.displayToSerial(Serial);
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}