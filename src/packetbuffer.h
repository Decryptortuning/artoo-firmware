#ifndef PACKETBUFFER_H
#define PACKETBUFFER_H

/*
 * Packet for transmitting messages to our host.
 * Packets are optionally framed via SLIP.
 */

#include <stdint.h>
#include <string.h>

#include "stm32/common.h"

namespace Slip {

static const unsigned END       = 0300; // indicates end of packet
static const unsigned ESC       = 0333; // indicates byte stuffing
static const unsigned ESC_END   = 0334; // ESC ESC_END means END data byte
static const unsigned ESC_ESC   = 0335; // ESC ESC_ESC means ESC data byte

}

template <unsigned tSize>
class PacketBuffer {

    uint8_t bytes[tSize];
    unsigned len;
    bool valid;

public:

    PacketBuffer() :
        len(0), valid(true)
    {}

    ALWAYS_INLINE void reset() {
        len = 0;
        valid = true;
    }

    ALWAYS_INLINE bool isFull() const {
        return len >= tSize;
    }

    static const unsigned Capacity = tSize;

    ALWAYS_INLINE bool ok() const { return valid; }

    ALWAYS_INLINE bool isEmpty() const {
        return len == 0;
    }

    ALWAYS_INLINE const uint8_t * data() const {
        return bytes;
    }

    ALWAYS_INLINE unsigned length() const {
        return len;
    }

    ALWAYS_INLINE unsigned bytesFree() const {
        return tSize - len;
    }

    ALWAYS_INLINE unsigned payloadLen() const {
        return (len == 0) ? 0 : len - 1;
    }

    ALWAYS_INLINE bool append(uint8_t b) {
        return append(&b, 1);
    }

    ALWAYS_INLINE bool append(const void *src, unsigned count) {
        if (!valid || count > bytesFree()) {
            valid = false;
            return false;
        }
        if (count) memcpy(bytes + len, src, count);
        len += count;
        return true;
    }

    bool appendSlip(uint8_t b) {
        if (b == Slip::END || b == Slip::ESC) {
            const uint8_t escaped[] = { Slip::ESC,
                static_cast<uint8_t>(b == Slip::END ? Slip::ESC_END : Slip::ESC_ESC) };
            return append(escaped, sizeof escaped);
        }
        return append(b);
    }

    bool appendSlip(const void *src, unsigned count) {
        const uint8_t *p = static_cast<const uint8_t*>(src);
        while (count--) {
            if (!appendSlip(*p++)) return false;
        }
        return valid;
    }

    template<typename T>
    bool appendItemSlip(const T &s) {
        return appendSlip(&s, sizeof s);
    }

    ALWAYS_INLINE bool delimitSlip() { return append(Slip::END); }

};

#endif // PACKETBUFFER_H
