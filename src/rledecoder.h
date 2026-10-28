#ifndef RLEDECODER_H
#define RLEDECODER_H
#include <stdint.h>
namespace RleDecoder {
// The existing palette RLE format, with explicit encoded and pixel budgets.
// emit(index,count) receives only validated palette indices/runs.
template<typename Emit>
bool decode(const uint8_t *data, unsigned bytes, unsigned pixels, unsigned maxIndex, Emit emit) {
    unsigned offset = 0, remaining = pixels;
    while (offset < bytes) {
        const int8_t header = static_cast<int8_t>(data[offset++]);
        if (header == -128) return remaining == 0 && offset == bytes;
        const unsigned count = header < 0 ? 1 - int(header) : unsigned(header) + 1;
        if (count > remaining) return false;
        if (header < 0) {
            if (offset == bytes || data[offset] > maxIndex) return false;
            emit(data[offset++], count);
        } else {
            if (count > bytes - offset) return false;
            for (unsigned i = 0; i < count; ++i) {
                if (data[offset] > maxIndex) return false;
                emit(data[offset++], 1);
            }
        }
        remaining -= count;
    }
    return false; // missing EOF
}
}
#endif
