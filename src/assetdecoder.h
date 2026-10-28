#ifndef ASSETDECODER_H
#define ASSETDECODER_H
#include <stdint.h>
namespace AssetDecoder {
typedef void (*Emit)(void *context,uint8_t index,unsigned count);
// Foreground-only; static bounded workspace rejects recursive/ISR entry.
// A rejected stream may already have emitted a validated pixel prefix.
bool decode(unsigned format,const uint8_t *data,unsigned bytes,unsigned pixels,
            unsigned maxIndex,Emit emit,void *context);
#ifdef ARTOO_HOST_TEST
// Test adapter changes only chunk boundaries, never format/decoder policy.
void testChunkSize(unsigned bytes);
#endif
}
#endif
