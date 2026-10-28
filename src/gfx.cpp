#include "gfx.h"
#include "assetdecoder.h"
#include "textlines.h"
#include "ili9341parallel.h"

#include <stdlib.h>

const unsigned Gfx::WIDTH = ILI9341Parallel::WIDTH;
const unsigned Gfx::HEIGHT = ILI9341Parallel::HEIGHT;

void Gfx::init()
{
    ILI9341Parallel::lcd.init();
    fillRect(Rect(0, 0, WIDTH, HEIGHT), 0x0);
}

void Gfx::drawPixelAtCursor(uint16_t color)
{
    static const unsigned AlphaKey = 0x3D3D;

    if (color == AlphaKey) {
        ILI9341Parallel::lcd.skipPixel();
    } else {
        ILI9341Parallel::lcd.drawPixelAtCursor(color);
    }
}

void Gfx::beginDrawingRegion(const Rect &r)
{
    ILI9341Parallel::lcd.beginDrawingRegion(r);
}

void Gfx::clear(uint16_t color)
{
    fillRect(Rect(0, 0, WIDTH, HEIGHT), color);
}

void Gfx::fillRect(const Rect & r, uint16_t color)
{
    if (!r.width || !r.height) return;
    ILI9341Parallel::lcd.setRect(r);
    ILI9341Parallel::lcd.fill(color, r.pixelCount());
}

void Gfx::drawImage(int16_t x, int16_t y, const ImageAsset &img) {
    drawBitmapBytes(x, y, static_cast<ImageFormat>(img.format), img.palette, static_cast<const uint8_t*>(img.data), img.encodedBytes, img.width, img.height);
}

uint16_t Gfx::drawImageCanvasHCentered(int16_t y, const ImageAsset &img) {
    uint16_t x = img.width <= WIDTH ? WIDTH/2 - img.width/2 : 0;
    drawBitmapBytes(x, y, static_cast<ImageFormat>(img.format), img.palette, static_cast<const uint8_t*>(img.data), img.encodedBytes, img.width, img.height);
    return x;
}

void Gfx::drawGlyph(char c, int16_t x, int16_t y, const FontAsset &font) {
    drawGlyph(c,x, y, font, NULL, NULL);
}

void Gfx::drawGlyph(char c, int16_t x, int16_t y, const FontAsset &font, uint16_t *color_fg, uint16_t *color_bg) {
    if (!font.hasGlyph(c)) return;
    const GlyphAsset &g = font.glyph(c);
    drawBitmapBytes(x, y, static_cast<ImageFormat>(font.format), font.palette, static_cast<const uint8_t*>(g.data), g.encodedBytes, g.width, g.height, color_fg, color_bg);
}

void Gfx::drawBitmapBytes(int16_t x, int16_t y, ImageFormat fmt, const ImagePalette *p, const uint8_t *b, unsigned encodedBytes, unsigned w, unsigned h)
{
    drawBitmapBytes(x, y, fmt, p, b, encodedBytes, w, h, NULL, NULL);
}

void Gfx::drawBitmapBytes(int16_t x, int16_t y, ImageFormat fmt, const ImagePalette *p, const uint8_t *b, unsigned encodedBytes, unsigned w, unsigned h, uint16_t *color_fg, uint16_t *color_bg)
{
    if ((fmt != ImgFmtRle && fmt != ImgFmtPacked) || !p || !b || !w || !h || w > WIDTH || h > HEIGHT) return;
    beginDrawingRegion(Rect(x, y, w, h));
    struct RenderContext {
        const ImagePalette *palette;
        FontColorConverter converter;
        bool grayscale;
    } context = {p, {}, bool(color_fg && color_bg)};
    if (context.grayscale) context.converter.init(*color_bg, *color_fg);
    AssetDecoder::decode(fmt,b,encodedBytes,w*h,p->maxIdx,
        [](void *opaque,uint8_t index,unsigned count) {
            RenderContext &render=*static_cast<RenderContext*>(opaque);
            uint16_t pixel=render.palette->color(index);
            if (render.grayscale) pixel=render.converter.grayToColor(pixel);
            while (count) {
                const unsigned chunk=MIN(count,8);
                switch (chunk) {
                case 8: drawPixelAtCursor(pixel); // fall through
                case 7: drawPixelAtCursor(pixel); // fall through
                case 6: drawPixelAtCursor(pixel); // fall through
                case 5: drawPixelAtCursor(pixel); // fall through
                case 4: drawPixelAtCursor(pixel); // fall through
                case 3: drawPixelAtCursor(pixel); // fall through
                case 2: drawPixelAtCursor(pixel); // fall through
                case 1: drawPixelAtCursor(pixel);
                case 0: break;
                }
                count-=chunk;
            }
        },&context);
}

uint16_t Gfx::write(uint16_t x, uint16_t y, const char *s, const FontAsset & font)
{
    return write(x, y, s, font, NULL, NULL);
}

uint16_t Gfx::write(uint16_t x, uint16_t y, const char *s,  const char *s_end, const FontAsset & font)
{
    return write(x, y, s, s_end, font, NULL, NULL);
}

uint16_t Gfx::write(uint16_t x, uint16_t y, const char *s, const FontAsset & font, uint16_t *color_fg, uint16_t *color_bg)
{
    /*
     * write a string with the given font.
     * if a character doesn't exist in this font, we just skip it for now.
     *
     * XXX: no wrapping support yet
     */

    for (; *s; s++) {
        char c = *s;
        if (font.hasGlyph(c)) {
            drawGlyph(c, x, y, font, color_fg, color_bg);
            x += font.glyph(c).width;
        }
    }

    return x;
}

uint16_t Gfx::write(uint16_t x, uint16_t y, const char *s,  const char *s_end, const FontAsset & font, uint16_t *color_fg, uint16_t *color_bg)
{
    /*
     * write a string with the given font from 's' to 's_end'
     * if a character doesn't exist in this font, we just skip it for now.
     *
     * XXX: no wrapping support yet
     */

    for (; *s; s++) {
        char c = *s;
        if (font.hasGlyph(c)) {
            drawGlyph(c, x, y, font, color_fg, color_bg);
            x += font.glyph(c).width;
        }
        if (s == s_end) {
            return x;
        }
    }

    return x;
}

void Gfx::writeRightJustified(uint16_t x, uint16_t y, unsigned w, const char *s, const FontAsset & font)
{
    unsigned strw = stringWidth(s, font);
    const unsigned offset = w > strw ? w - strw : 0;
    write(x + MIN(offset, unsigned(UINT16_MAX - x)), y, s, font);
}

void Gfx::writeCenterJustified(uint16_t x, uint16_t y, unsigned center, const char *s, const FontAsset & font)
{
    const unsigned halfWidth = stringWidth(s, font)/2;
    const unsigned offset = center > halfWidth ? center - halfWidth : 0;
    write(x + MIN(offset, unsigned(UINT16_MAX - x)), y, s, font);
}

void Gfx::writeCanvasCenterJustified(uint16_t y, const char *s, const FontAsset & font)
{
    writeCanvasCenterJustified(y, s, font, 0, NULL, NULL);
}

void Gfx::writeCanvasCenterJustified(uint16_t y, const char *s, const FontAsset & font, uint16_t *color_fg, uint16_t *color_bg)
{
    writeCanvasCenterJustified(y, s, font, 0, color_fg, color_bg);
}

void Gfx::writeCanvasCenterJustified(uint16_t y, const char *s, const FontAsset & font, uint16_t line_spacing_h)
{
    writeCanvasCenterJustified(y, s, font, line_spacing_h, NULL, NULL);
}

void Gfx::writeCanvasCenterJustified(uint16_t y, const char *s, const FontAsset & font, uint16_t line_spacing_h, uint16_t *color_fg, uint16_t *color_bg)
{
    /*
     * write a single string with delimiting characters on multiple lines
     *
     */

    uint16_t yCurrent = y;
    TextLines::visit(s, [&](const char *begin, size_t length) {
        if (length) {
            const unsigned width = stringWidth(begin, begin + length, font);
            const unsigned x = width < WIDTH ? WIDTH / 2 - width / 2 : 0;
            write(x, yCurrent, begin, begin + length - 1, font, color_fg, color_bg);
        }
        yCurrent += font.height() + line_spacing_h;
    });
}

uint16_t Gfx::writeMonospace(uint16_t x, uint16_t y, const char *s, const FontAsset & font, char box,
                             uint16_t *color_fg, uint16_t *color_bg)
{
    if (!font.hasGlyph(box)) return x;

    uint8_t boxW = font.glyph(box).width;

    for (; *s; s++) {
        char c = *s;
        if (font.hasGlyph(c)) {
            // right justify within the box
            const unsigned width = font.glyph(c).width;
            const unsigned offset = boxW > width ? boxW - width : 0;
            uint16_t xoff = x + MIN(offset, unsigned(UINT16_MAX - x));
            drawGlyph(c, xoff, y, font, color_fg, color_bg);
            x += boxW;
        }
    }
    return x;
}

uint16_t Gfx::stringWidth(const char *s, const FontAsset & font)
{
    /*
     * Calculate the width of the string 's' using 'font'.
     */

    uint16_t w = 0;

    for (; *s; s++) {
        char c = *s;
        if (font.hasGlyph(c)) {
            w += font.glyph(c).width;
        }
    }

    return w;
}

uint16_t Gfx::stringWidth(const char *s, const char *s_end, const FontAsset & font)
{
    /*
     * Calculate the width of the string 's' until character at 's_end' using 'font'.
     */

    uint16_t w = 0;

    for (; *s; s++) {
        if (s == s_end) {
            return w;
        }
        char c = *s;
        if (font.hasGlyph(c)) {
            w += font.glyph(c).width;
        }
    }

    return w;
}

void Gfx::FontColorConverter::init(uint16_t color_fg, uint16_t color_bg) {
    R1 = R(color_fg);
    R2 = R(color_bg);
    G1 = G(color_fg);
    G2 = G(color_bg);
    B1 = B(color_fg);
    B2 = B(color_bg);
}

