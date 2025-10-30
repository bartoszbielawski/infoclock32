
#include <freertos/FreeRTOS.h>
#include "graphic_utils.hpp"
#include <resource_manager.hpp>
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

//put it here to avoid reallocation on each call
//and it can be bigger as well because it doesn't belng to any task stack
static GFXcanvas1 canvas(512, 8);




void scrollMessage(std::string message, LMDS& display, int speed, int steps)
{ 
  auto msgLength = message.size();
  static const int FONT_WIDTH = 6; //5 pixels + 1 pixel space

  Serial.printf("Scrolling message: '%s', length: %d\n", message.c_str(), msgLength);

  //center shorter messages
  if (msgLength * FONT_WIDTH <= display.width())
  {
    Serial.println("Message fits on the display, centering");
    //message fits on the display, no need to scroll, but center the message
    display.clear();
    int offset = (display.width() - msgLength) / 2;
    copyCanvasToDisplay(canvas, 0, display, offset);
    display.displayToSerial(Serial);
    vTaskDelay(10 * speed / portTICK_PERIOD_MS);
    return;
  }

  GFXcanvas1 canvas(msgLength * FONT_WIDTH, 8);
  canvas.print(message.c_str());

  vTaskDelay(10 * speed / portTICK_PERIOD_MS);

  for (int i = 0; i <= canvas.width() - display.width() + steps; i += steps)
  {
    copyCanvasToDisplay(canvas, i, display, 0);
    display.displayToSerial(Serial);
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
  
  vTaskDelay(10 * speed / portTICK_PERIOD_MS);
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

void scrollOutDisplayRight(LMDS& display, int speed)
{
  //assume you already have access to the display
  for (int col = 0; col < display.getSegments() * 8; col++)
  {
    display.scroll(LMDS::scrollDirection::scrollRight);
    display.displayToSerial(Serial);
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}