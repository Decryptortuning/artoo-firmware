#include "lcd_sink.h"
#include <assert.h>
#include <stdio.h>
#include <string>
ILI9341Parallel ILI9341Parallel::lcd;
std::vector<Gfx::Rect> ILI9341Parallel::regions;
std::vector<uint16_t> ILI9341Parallel::pixels;
static void clearSink() {
    ILI9341Parallel::regions.clear(); ILI9341Parallel::pixels.clear();
}
int main() {
    const uint16_t colors[] = {0x3d3d,0xffff};
    const Gfx::ImagePalette palette = {colors,1};
    const uint8_t small[] = {0xfe,1,0x80}; // three white pixels
    const uint8_t large[] = {0xfc,1,0x80}; // five white pixels
    const Gfx::GlyphAsset glyphs[] = {{small,3,1,sizeof small},{large,5,1,sizeof large}};
    const Gfx::FontAsset font = {2,1,0,Gfx::ImgFmtRle,'a',0,0,glyphs,&palette};
    auto &regions = ILI9341Parallel::regions;
    auto &pixels = ILI9341Parallel::pixels;
    Gfx::fillRect(Gfx::Rect(0,0,0,10),7);
    Gfx::fillRect(Gfx::Rect(0,0,10,0),7);
    assert(regions.empty() && pixels.empty());
    Gfx::fillRect(Gfx::Rect(2,3,4,5),7);
    assert(regions.size()==1 && pixels.size()==20 && pixels[0]==7);
    clearSink();
    assert(Gfx::writeMonospace(9,0,"ab",font,'x')==9);
    Gfx::drawGlyph('x',0,0,font);
    assert(regions.empty());
    assert(Gfx::writeMonospace(0,0,"b",font,'a')==3);
    assert(regions.size()==1 && regions[0].x==0 && pixels.size()==5);
    clearSink();
    Gfx::writeRightJustified(4,0,2,"ab",font);
    assert(regions[0].x==4); // overwide label starts at the supplied origin
    clearSink();
    Gfx::writeRightJustified(4,0,10,"ab",font);
    assert(regions[0].x==6); // valid placement unchanged
    clearSink();
    Gfx::writeCenterJustified(4,0,2,"ab",font);
    assert(regions[0].x==4);
    clearSink();
    Gfx::writeCenterJustified(4,0,10,"ab",font);
    assert(regions[0].x==10);
    clearSink();
    Gfx::writeCanvasCenterJustified(2,"a\n\nb\n",font);
    assert(regions.size()==2 && regions[0].x==159 && regions[1].x==158);
    assert(regions[0].y==2 && regions[1].y==4);
    clearSink();
    Gfx::writeCanvasCenterJustified(2,"\n\n",font);
    assert(regions.empty());
    const std::string overwide(70,'b');
    Gfx::writeCanvasCenterJustified(2,overwide.c_str(),font);
    assert(regions[0].x==0);
    clearSink();
    const uint8_t transparent[] = {0,0,0x80};
    const Gfx::ImageAsset image = {1,1,Gfx::ImgFmtRle,0,transparent,&palette,sizeof transparent};
    Gfx::drawImage(0,0,image);
    assert(pixels.size()==1 && pixels[0]==0x3d3d);
    clearSink();
    const Gfx::ImageAsset empty = {0,1,Gfx::ImgFmtRle,0,transparent,&palette,sizeof transparent};
    Gfx::drawImage(0,0,empty);
    assert(regions.empty());
    puts("Actual Gfx wrappers: empty rectangles, missing glyphs, bounded placement, newline traversal and RGB565 sink passed.");
}
