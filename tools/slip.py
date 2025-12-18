#!/usr/bin/env python3

class SlipDevice:

    END = 0xC0  # indicates end of packet
    ESC = 0xDB  # indicates byte stuffing
    ESC_END = 0xDC  # ESC ESC_END means END data byte
    ESC_ESC = 0xDD  # ESC ESC_ESC means ESC data byte

    def __init__(self, serial_dev, trace_tx=False):
        self.ser = serial_dev
        self.trace_tx = trace_tx
        self.insync = False

        try:
            self.ser.reset_input_buffer()
        except AttributeError:
            self.ser.flushInput()
        try:
            self.ser.reset_output_buffer()
        except AttributeError:
            self.ser.flushOutput()

    def _read_byte(self):
        b = self.ser.read(1)
        if not b:
            raise TimeoutError("Timed out waiting for serial byte")
        return b[0]

    def sync(self):
        while self._read_byte() != self.END:
            pass
        self.insync = True

    def read(self):
        """
        Read a SLIP packet from Artoo.

        Returns:
            bytes: Decoded packet (without END framing bytes).
        """
        if not self.insync:
            self.sync()

        pkt = bytearray()

        while True:
            b = self._read_byte()
            if b == self.END:
                if pkt:
                    return bytes(pkt)
            elif b == self.ESC:
                b = self._read_byte()
                if b == self.ESC_END:
                    pkt.append(self.END)
                elif b == self.ESC_ESC:
                    pkt.append(self.ESC)
                else:
                    pkt.append(b)
            else:
                pkt.append(b)

    def write(self, pkt):
        """
        Write a SLIP message to Artoo.

        Args:
            pkt: bytes/bytearray, or an iterable of ints/1-byte bytes/1-char strs.
        """
        if isinstance(pkt, str):
            pkt_bytes = pkt.encode("utf-8")
        elif isinstance(pkt, (bytes, bytearray)):
            pkt_bytes = bytes(pkt)
        else:
            buf = bytearray()
            for b in pkt:
                if isinstance(b, int):
                    buf.append(b & 0xFF)
                elif isinstance(b, (bytes, bytearray)):
                    if len(b) != 1:
                        raise ValueError("Expected 1-byte element")
                    buf.append(b[0])
                elif isinstance(b, str):
                    if len(b) != 1:
                        raise ValueError("Expected 1-char element")
                    buf.append(ord(b))
                else:
                    raise TypeError("Unsupported element type: %r" % (type(b),))
            pkt_bytes = bytes(buf)

        slip_bytes = bytearray([self.END])
        for b in pkt_bytes:
            if b == self.END:
                slip_bytes.extend([self.ESC, self.ESC_END])
            elif b == self.ESC:
                slip_bytes.extend([self.ESC, self.ESC_ESC])
            else:
                slip_bytes.append(b)

        slip_bytes.append(self.END)
        if self.trace_tx:
            print("r2 tx:", [hex(b) for b in slip_bytes])
        return self.ser.write(slip_bytes)
