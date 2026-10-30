#include "src/diagnosticrecord.h"
#include "src/diagnostics.h"
#include "src/hostmessage.h"
#include "src/slipdecoder.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <initializer_list>
int main() {
    uint32_t words[16] = {};
    assert(!DiagnosticRecord::valid(words));
    words[0] = DiagnosticRecord::Magic;
    words[1] = DiagnosticRecord::Format;
    words[2] = 3;
    words[3] = 0xfffffff9;
    words[4] = 0x2000bfe0;
    words[5] = 0x08001234;
    words[13] = 1;
    words[15] = DiagnosticRecord::checksum(words);
    assert(DiagnosticRecord::valid(words));
    for (unsigned i = 0; i < 16; ++i) {
        for (unsigned bit = 0; bit < 32; ++bit) {
            words[i] ^= 1u << bit;
            assert(!DiagnosticRecord::valid(words));
            words[i] ^= 1u << bit;
        }
    }
    words[13] = 2;
    for (unsigned stage = 1; stage <= 13; ++stage) {
        words[14] = stage; words[15] = DiagnosticRecord::checksum(words);
        assert(DiagnosticRecord::valid(words));
    }
    for (unsigned stage : {0u,14u,0xffffffffu}) {
        words[14] = stage; words[15] = DiagnosticRecord::checksum(words);
        assert(!DiagnosticRecord::valid(words));
    }
    words[14] = 1; words[13] = 3; words[15] = DiagnosticRecord::checksum(words);
    assert(!DiagnosticRecord::valid(words));
    words[0] = 0; // Partial commit must never appear valid.
    assert(!DiagnosticRecord::valid(words));
    assert(DiagnosticRecord::stackValid(0x20000000,0xfffffff9,0));
    assert(DiagnosticRecord::stackValid(0x2000bfe0,0xfffffffd,0));
    assert(!DiagnosticRecord::stackValid(0x2000bfe4,0xfffffff9,0));
    assert(!DiagnosticRecord::stackValid(0x20000002,0xfffffff9,0));
    assert(!DiagnosticRecord::stackValid(0x1ffffffc,0xfffffff9,0));
    assert(!DiagnosticRecord::stackValid(0xffffffff,0xfffffff9,0));
    assert(!DiagnosticRecord::stackValid(0x2000bfe0,0,0));
    for (unsigned mask = 1; mask; mask <<= 1)
        if (mask & 0x1818) assert(!DiagnosticRecord::stackValid(0x2000bfe0,0xfffffff9,mask));
    uint8_t encoded[4];
    DiagnosticRecord::encode32(encoded,0x12db34c0);
    const uint8_t expected[] = {0xc0,0x34,0xdb,0x12};
    assert(memcmp(encoded,expected,4) == 0);
    const uint8_t request[] = {1,0xc0,0xdb};
    assert(HostMessage::Diagnostics == 27);
    assert(HostMessage::payloadValid(27,request,3));
    for (unsigned n = 0; n < 300; ++n) if (n != 3)
        assert(!HostMessage::payloadValid(27,request,n));
    const uint8_t bad[] = {2,0,0};
    assert(!HostMessage::payloadValid(27,bad,3));
    assert(HostMessage::requestEnabled(27));
    // Worst possible reply escaping fits the existing firmware envelope.
    PacketBuffer<530> packet;
    packet.delimitSlip(); packet.appendSlip(27);
    for (unsigned i = 0; i < Diagnostics::ReplyBytes; ++i) packet.appendSlip(i % 2 ? 0xc0 : 0xdb);
    packet.delimitSlip();
    assert(packet.ok() && packet.length() == 411);
    SlipDecoder<264> decoder;
    unsigned complete = 0;
    for (unsigned i = 0; i < packet.length(); ++i)
        if (decoder.consume(packet.data()[i]) == SlipDecoder<264>::Complete) {
            ++complete;
            assert(decoder.packet().length() == 205);
            assert(decoder.packet().data()[0] == 27);
        }
    assert(complete == 1);
    puts("diagnostics record integrity, frame validation, request schema and worst SLIP envelope pass");
}
