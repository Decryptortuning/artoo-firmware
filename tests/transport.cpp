#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "src/hostmessage.h"
#include "src/slipdecoder.h"
#include "SLIP.h" // Actual SoloLink consumer, compiled into this host test.

static_assert(CHAR_MIN == 0, "actual SoloLink SLIP fixture must model ARM char ABI");

static std::vector<uint8_t> linuxDecode(const uint8_t *bytes, unsigned length)
{
    char storage[1024] = {};
    SLIPDecoder decoder(storage, sizeof storage);
    std::vector<uint8_t> result;
    for (unsigned i = 0; i < length; ++i) {
        char byte = static_cast<char>(bytes[i]);
        const int n = decoder.addByte(&byte);
        assert(n >= 0);
        if (n) {
            assert(result.empty());
            result.assign(storage, storage + n);
        }
    }
    return result;
}

static void packetBounds()
{
    PacketBuffer<4> packet;
    assert(packet.append(1));
    assert(packet.appendSlip(Slip::END));
    assert(packet.length() == 3);
    assert(!packet.appendSlip(Slip::ESC)); // no half-escape written
    assert(packet.length() == 3 && !packet.ok());
    assert(!packet.append(2)); // failure is sticky until reset
    packet.reset();
    const uint8_t data[] = {1,2,3,4};
    assert(packet.append(data, 4) && packet.isFull());
    assert(!packet.append(data, 1) && packet.length() == 4);
    assert(memcmp(packet.data(), data, 4) == 0);

    for (unsigned size = 0; size <= 263; ++size) {
        PacketBuffer<530> wire;
        std::vector<uint8_t> raw(1, HostMessage::Mavlink);
        for (unsigned i = 0; i < size; ++i)
            raw.push_back(i & 1 ? Slip::ESC : Slip::END);
        assert(wire.delimitSlip());
        assert(wire.appendSlip(raw.data(), raw.size()));
        assert(wire.delimitSlip() && wire.ok());
        assert(linuxDecode(wire.data(), wire.length()) == raw);
        SlipDecoder<264> decoder;
        unsigned completions = 0;
        for (unsigned i = 0; i < wire.length(); ++i) {
            if (decoder.consume(wire.data()[i]) == SlipDecoder<264>::Complete) {
                ++completions;
                assert(decoder.packet().length() == raw.size());
                assert(memcmp(decoder.packet().data(), raw.data(), raw.size()) == 0);
            }
        }
        assert(completions == 1);
    }
    // Every possible byte, including both reserved values, crosses both codecs.
    PacketBuffer<530> wire;
    uint8_t all[256];
    for (unsigned i = 0; i < 256; ++i) all[i] = i;
    wire.delimitSlip(); wire.appendSlip(all, sizeof all); wire.delimitSlip();
    const std::vector<uint8_t> decoded = linuxDecode(wire.data(), wire.length());
    assert(decoded.size() == sizeof all && !memcmp(decoded.data(), all, sizeof all));

    // StoredValues is 172 bytes; worst escaping exceeds the old 263-byte TX.
    uint8_t values[172]; memset(values, Slip::END, sizeof values);
    wire.reset(); wire.delimitSlip(); wire.appendSlip(HostMessage::ParamStoredVals);
    wire.appendSlip(values, sizeof values); wire.delimitSlip();
    assert(wire.ok() && wire.length() == 347);
    assert(linuxDecode(wire.data(), wire.length()).size() == 173);
}

static void buttons()
{
    for (unsigned mask = 0; mask < 512; ++mask) {
        for (unsigned id = 0; id < 9; ++id) {
            for (unsigned event = 0; event < 9; ++event) {
                PacketBuffer<530> packet;
                assert(HostMessage::appendButtonEvent(packet, id, event, mask));
                const std::vector<uint8_t> raw = linuxDecode(packet.data(), packet.length());
                assert(raw.size() == 5 && raw[0] == HostMessage::ButtonEvent);
                assert(raw[1] == id && raw[2] == event);
                assert(raw[3] == (mask & 255) && raw[4] == (mask >> 8));
            }
        }
    }
}

static void rejectAndRecover()
{
    SlipDecoder<4> decoder;
    assert(decoder.consume(10) == SlipDecoder<4>::Pending); // unsynchronized
    decoder.consume(Slip::END);
    for (unsigned i = 0; i < 4; ++i) decoder.consume(i);
    assert(decoder.consume(5) == SlipDecoder<4>::Rejected);
    assert(decoder.consume(6) == SlipDecoder<4>::Pending);
    assert(decoder.consume(Slip::END) == SlipDecoder<4>::Pending); // reject prefix
    decoder.consume(42);
    assert(decoder.consume(Slip::END) == SlipDecoder<4>::Complete);
    assert(decoder.packet().length() == 1 && decoder.packet().data()[0] == 42);
    decoder.consume(Slip::ESC);
    assert(decoder.consume(Slip::END) == SlipDecoder<4>::Rejected);
    decoder.consume(43);
    assert(decoder.consume(Slip::END) == SlipDecoder<4>::Complete);
    decoder.consume(Slip::ESC);
    assert(decoder.consume(0) == SlipDecoder<4>::Rejected);
    decoder.consume(99);
    assert(decoder.consume(Slip::END) == SlipDecoder<4>::Pending);
    decoder.consume(44);
    decoder.reset(); // UART discontinuity drops the incomplete frame
    decoder.consume(45);
    assert(decoder.consume(Slip::END) == SlipDecoder<4>::Pending);

    uint32_t random = 0x3d202610;
    for (unsigned i = 0; i < 1000000; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        if (decoder.consume(random & 255) == SlipDecoder<4>::Complete) {
            assert(decoder.packet().ok());
            assert(decoder.packet().length() > 0 && decoder.packet().length() <= 4);
        }
    }
}

static void payloads()
{
    // Exact current producer path: SysInfo.cpp::ping sends ' '; the generic
    // PacketHandler prepends ID3 and invokes this actual Linux SLIP encoder.
    char ping[] = {HostMessage::SysInfo, ' '};
    char encoded[16] = {};
    SLIPEncoder linuxEncoder(encoded, sizeof encoded);
    const int encodedLength = linuxEncoder.encode(ping, sizeof ping);
    assert(encodedLength > 0);
    SlipDecoder<264> artooDecoder;
    bool completed = false;
    for (int i = 0; i < encodedLength; ++i) {
        if (artooDecoder.consume(static_cast<uint8_t>(encoded[i])) == SlipDecoder<264>::Complete) {
            const PacketBuffer<264> &packet = artooDecoder.packet();
            assert(packet.length() == 2 && packet.data()[0] == HostMessage::SysInfo);
            assert(HostMessage::payloadValid(packet.data()[0], packet.data() + 1, packet.payloadLen()));
            completed = true;
        }
    }
    assert(completed);
    assert(HostMessage::payloadValid(HostMessage::SysInfo, NULL, 0)); // boot updater
    for (unsigned byte = 0; byte < 256; ++byte) {
        const uint8_t value = byte;
        assert(HostMessage::payloadValid(HostMessage::SysInfo, &value, 1) == (byte == ' '));
    }
    assert(!HostMessage::payloadLengthValid(HostMessage::SetRawIo, 0));
    assert(HostMessage::payloadLengthValid(HostMessage::SetRawIo, 1));
    assert(!HostMessage::payloadLengthValid(HostMessage::SetRawIo, 2));
    assert(HostMessage::payloadLengthValid(HostMessage::Calibrate, 36));
    assert(!HostMessage::payloadLengthValid(HostMessage::Calibrate, 35));
    assert(HostMessage::payloadLengthValid(HostMessage::ConfigStickAxes, 48));
    assert(HostMessage::payloadLengthValid(HostMessage::ConfigSweepTime, 8));
    assert(!HostMessage::payloadLengthValid(255, 0));
    for (unsigned length = 0; length <= 25; ++length) {
        std::vector<uint8_t> wire(length, 'x');
        if (length >= 4) { wire[0] = 4; wire[1] = 0; wire[2] = 255; wire[3] = 3; }
        uint8_t output[26]; memset(output, 0xa5, sizeof output);
        const bool valid = HostMessage::decodeButtonConfig(output + 1, wire.data(), length);
        assert(valid == (length >= 4 && length <= 24));
        assert(output[0] == 0xa5 && output[25] == 0xa5);
        if (valid) {
            assert(output[1] == 4 && output[24] == 0);
            assert(strlen(reinterpret_cast<char*>(output + 5)) == (length < 24 ? length - 4 : 19));
        }
    }
    for (unsigned length = 0; length <= 64; ++length) {
        std::vector<uint8_t> text(length, 'S');
        char dest[65];
        HostMessage::copyText(dest, sizeof dest, text.data(), length);
        assert(strlen(dest) == length);
    }
    const uint8_t good[] = {'S','o','l','o',0};
    const uint8_t suffix[] = {'S',0,'x',0};
    assert(HostMessage::pairingTextValid(good, sizeof good));
    assert(!HostMessage::pairingTextValid(good, sizeof good - 1));
    assert(!HostMessage::pairingTextValid(suffix, sizeof suffix));
    assert(!HostMessage::pairingTextValid(NULL, 0));
}

int main()
{
    packetBounds(); buttons(); rejectAndRecover(); payloads();
    puts("Transport bounds, all 41472 button cases, malformed/recovery streams, payloads and Linux decoder passed.");
}
