#ifndef SLIPDECODER_H
#define SLIPDECODER_H

#include "packetbuffer.h"

// A rejected frame stays rejected until a delimiter. Completed bytes remain
// available until the next consume(), so the consumer never receives a prefix.
template<unsigned Capacity>
class SlipDecoder {
    PacketBuffer<Capacity> buffer;
    bool synchronized;
    bool escaped;
    bool complete;

public:
    enum Result { Pending, Complete, Rejected };

    SlipDecoder() : buffer(), synchronized(false), escaped(false), complete(false) {}

    void reset() {
        buffer.reset();
        synchronized = escaped = complete = false;
    }

    const PacketBuffer<Capacity> &packet() const { return buffer; }

    Result consume(uint8_t byte) {
        if (complete) {
            buffer.reset();
            complete = false;
        }
        if (!synchronized) {
            if (byte == Slip::END) {
                buffer.reset();
                synchronized = true;
                escaped = false;
            }
            return Pending;
        }
        if (byte == Slip::END) {
            if (escaped) {
                buffer.reset();
                escaped = false;
                return Rejected;
            }
            complete = !buffer.isEmpty();
            return complete ? Complete : Pending;
        }
        if (escaped) {
            escaped = false;
            if (byte == Slip::ESC_END) byte = Slip::END;
            else if (byte == Slip::ESC_ESC) byte = Slip::ESC;
            else {
                reset();
                return Rejected;
            }
        } else if (byte == Slip::ESC) {
            escaped = true;
            return Pending;
        }
        if (!buffer.append(byte)) {
            reset();
            return Rejected;
        }
        return Pending;
    }
};

#endif
