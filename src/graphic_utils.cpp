
#include <freertos/FreeRTOS.h>
#include "graphic_utils.hpp"
#include <resource_manager.hpp>
#include <logger.hpp>
#include <memory>

ResourceManager<LMDS> displayManager;

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


void scrollMessage(std::string message, LMDS& display, int speed, int steps)
{
  //message = substituteGlyphs(message);
  auto msgLength = message.size();
  static const int FONT_WIDTH = 6; //5 pixels + 1 pixel space

  logPrintf("DISP", "scrolling '%s'", message.c_str());

  //center shorter messages
  if (msgLength * FONT_WIDTH <= (size_t)display.width())
  {
    logPrintf("DISP", "message fits on display, centering without scrolling");
    GFXcanvas1 canvas(msgLength * FONT_WIDTH, 8);
    canvas.print(message.c_str());
    display.clear();
    int offset = (display.width() - (int)(msgLength * FONT_WIDTH)) / 2;
    copyCanvasToDisplay(canvas, 0, display, offset);
    display.display();
    vTaskDelay(10 * speed / portTICK_PERIOD_MS);
    return;
  }

  GFXcanvas1 canvas(msgLength * FONT_WIDTH, 8);
  canvas.print(message.c_str());

  vTaskDelay(10 * speed / portTICK_PERIOD_MS);

  size_t last = canvas.width() - display.width() + steps;
  for (int i = 0; i <= last; i += steps)
  {    
    copyCanvasToDisplay(canvas, i, display, 0);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
    if (i == 0) vTaskDelay(10 * speed / portTICK_PERIOD_MS);
    if (i == last) vTaskDelay(10 * speed / portTICK_PERIOD_MS);    
  }
  
  vTaskDelay(10 * speed / portTICK_PERIOD_MS);
}

void wipeDisplayLeftToRight(LMDS& display, int speed)
{
  //assume you already have access to the display
  for (int col = 0; col < display.getSegments() * 8; col++)
  {
    display.setColumn(col, 0x00);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

void scrollOutDisplayRight(LMDS& display, int speed)
{
  //assume you already have access to the display
  for (int col = 0; col < display.getSegments() * 8; col++)
  {
    display.scroll(LMDS::scrollDirection::scrollRight);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}