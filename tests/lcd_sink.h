#ifndef ARTOO_TEST_LCD_SINK_H
#define ARTOO_TEST_LCD_SINK_H
// Replace only the physical LCD sink when compiling the real gfx.cpp on host.
#define ILI9341_PARALLEL_H
#include "src/gfx.h"
#include <vector>
class ILI9341Parallel {
public:
    static const unsigned WIDTH = 320, HEIGHT = 240;
    static ILI9341Parallel lcd;
    static std::vector<Gfx::Rect> regions;
    static std::vector<uint16_t> pixels;
    void init() {}
    void setRect(const Gfx::Rect &r) { regions.push_back(r); }
    void beginDrawingRegion(const Gfx::Rect &r) { setRect(r); }
    void fill(uint16_t color,unsigned count) { pixels.insert(pixels.end(),count,color); }
    void drawPixelAtCursor(uint16_t color) { pixels.push_back(color); }
    void skipPixel() { pixels.push_back(0x3d3d); }
};
#endif
