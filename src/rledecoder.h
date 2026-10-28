#ifndef RLEDECODER_H
#define RLEDECODER_H
#include <stdint.h>
namespace RleDecoder {
// Incremental parser preserves count/index/EOF validation across Tamp chunks.
class Stream {
    unsigned remaining,maximum,count;
    bool repeat,ended;
public:
    Stream(unsigned pixels,unsigned maxIndex) : remaining(pixels),maximum(maxIndex),count(0),repeat(false),ended(false) {}
    template<typename Emit> bool put(uint8_t value,Emit emit) {
        if (ended) return false;
        if (!count) {
            const int8_t header=static_cast<int8_t>(value);
            if (header==-128) { ended=true; return remaining==0; }
            repeat=header<0;
            count=repeat ? 1-int(header) : unsigned(header)+1;
            return count<=remaining;
        }
        if (value>maximum) return false;
        const unsigned pixels=repeat ? count : 1;
        emit(value,pixels);
        remaining-=pixels;
        count-=pixels;
        return true;
    }
    bool finish() const { return ended && remaining==0 && count==0; }
};
template<typename Emit>
bool decode(const uint8_t *data,unsigned bytes,unsigned pixels,unsigned maxIndex,Emit emit) {
    Stream stream(pixels,maxIndex);
    for (unsigned i=0;i<bytes;++i) if (!stream.put(data[i],emit)) return false;
    return stream.finish();
}
}
#endif
