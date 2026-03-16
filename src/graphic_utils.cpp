
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
    vTaskDelay(100 * speed / portTICK_PERIOD_MS);
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
    if (i == 0) vTaskDelay(50 * speed / portTICK_PERIOD_MS);
    if (i == last) vTaskDelay(50 * speed / portTICK_PERIOD_MS);    
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

// Mirror of scrollOutDisplayRight
void scrollOutDisplayLeft(LMDS& display, int speed)
{
  for (int col = 0; col < display.getSegments() * 8; col++)
  {
    display.scroll(LMDS::scrollDirection::scrollLeft);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

// Clear rows one at a time, top → bottom
void wipeTopToBottom(LMDS& display, int speed)
{
  int width = display.getSegments() * 8;
  for (int y = 0; y < 8; y++)
  {
    for (int x = 0; x < width; x++)
      display.setPixel(x, y, false);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

// Top and bottom rows collapse toward the centre simultaneously (4 steps)
void wipeCurtain(LMDS& display, int speed)
{
  int width = display.getSegments() * 8;
  for (int i = 0; i < 4; i++)
  {
    for (int x = 0; x < width; x++)
    {
      display.setPixel(x, i,     false);
      display.setPixel(x, 7 - i, false);
    }
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

// Fill display with random static, then clear it — both in random column order
void wipeStaticNoise(LMDS& display, int speed)
{
  int width = display.getSegments() * 8;

  // Build column index array and Fisher-Yates shuffle it
  uint8_t cols[width];
  for (int i = 0; i < width; i++) cols[i] = i;

  auto shuffle = [&]() {
    for (int i = width - 1; i > 0; i--) {
      int j = random(i + 1);
      uint8_t tmp = cols[i]; cols[i] = cols[j]; cols[j] = tmp;
    }
  };

  // Phase 1: fill columns with random pixels in shuffled order
  shuffle();
  for (int i = 0; i < width; i++)
  {
    display.setColumn(cols[i], random(256));
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }

  // Phase 2: clear columns in a new shuffled order
  shuffle();
  for (int i = 0; i < width; i++)
  {
    display.setColumn(cols[i], 0x00);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

// Scroll content upward and off the top edge (8 steps)
void scrollOutDisplayUp(LMDS& display, int speed)
{
  for (int row = 0; row < 8; row++)
  {
    display.scroll(LMDS::scrollDirection::scrollUp);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

// Scroll content downward and off the bottom edge (8 steps)
void scrollOutDisplayDown(LMDS& display, int speed)
{
  for (int row = 0; row < 8; row++)
  {
    display.scroll(LMDS::scrollDirection::scrollDown);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

// Clear even rows first, then odd rows — venetian-blinds effect (2 steps)
void wipeVenetianBlinds(LMDS& display, int speed)
{
  int width = display.getSegments() * 8;
  for (int phase = 0; phase < 2; phase++)
  {
    for (int y = phase; y < 8; y += 2)
      for (int x = 0; x < width; x++)
        display.setPixel(x, y, false);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

// Spiral inward: clear pixels one at a time following a rectangular spiral from the
// outermost ring toward the centre.  With 8 rows and 64 cols there are 4 rings.
// speed = ms per pixel (default 1 ms → ~512 ms total).
void wipeSpiralInward(LMDS& display, int speed)
{
  int width  = display.getSegments() * 8;
  int top    = 0, bottom = 7, left = 0, right = width - 1;

  while (top <= bottom && left <= right)
  {
    // Top row: left → right
    for (int x = left; x <= right; x++) {
      display.setPixel(x, top, false);
      display.display();
      vTaskDelay(speed / portTICK_PERIOD_MS);
    }
    top++;

    // Right column: top → bottom
    for (int y = top; y <= bottom; y++) {
      display.setPixel(right, y, false);
      display.display();
      vTaskDelay(speed / portTICK_PERIOD_MS);
    }
    right--;

    // Bottom row: right → left (guard against single-row remainder)
    if (top <= bottom) {
      for (int x = right; x >= left; x--) {
        display.setPixel(x, bottom, false);
        display.display();
        vTaskDelay(speed / portTICK_PERIOD_MS);
      }
      bottom--;
    }

    // Left column: bottom → top (guard against single-column remainder)
    if (left <= right) {
      for (int y = bottom; y >= top; y--) {
        display.setPixel(left, y, false);
        display.display();
        vTaskDelay(speed / portTICK_PERIOD_MS);
      }
      left++;
    }
  }
}

// NW→SE diagonal sweep: clear one anti-diagonal per step.
// Diagonals are indexed by d = x + y (0 … width+6).
// speed = ms per diagonal (default 15 ms → ~1 s total for 64-wide display).
void wipeDiagonal(LMDS& display, int speed)
{
  int width = display.getSegments() * 8;
  for (int d = 0; d < width + 8; d++)
  {
    for (int y = 0; y < 8; y++) {
      int x = d - y;
      if (x >= 0 && x < width)
        display.setPixel(x, y, false);
    }
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

// Horizontal curtain: clear columns from both edges simultaneously toward the centre.
// speed = ms per step (32 steps for a 64-wide display).
void wipeSplitToCenter(LMDS& display, int speed)
{
  int width = display.getSegments() * 8;
  for (int i = 0; i < width / 2; i++)
  {
    display.setColumn(i,           0x00);
    display.setColumn(width - 1 - i, 0x00);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}

// Reverse horizontal curtain: clear columns from the centre outward to both edges.
// speed = ms per step (32 steps for a 64-wide display).
void wipeColumnsFromCenter(LMDS& display, int speed)
{
  int width  = display.getSegments() * 8;
  int center = width / 2;
  for (int i = 0; i < center; i++)
  {
    display.setColumn(center - 1 - i, 0x00);
    display.setColumn(center     + i, 0x00);
    display.display();
    vTaskDelay(speed / portTICK_PERIOD_MS);
  }
}