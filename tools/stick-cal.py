#!/usr/bin/env python3

import sys, os, struct
import serial, slip

ARTOO_BAUD = 115200

ARTOO_RAW_IO_ID = 0x05
ARTOO_RAW_IO_REPORT_ID = 0x06
ARTOO_SYSINFO_ID = 0x03
ARTOO_CALIBRATE_ID = 0x02
ARTOO_DSM_ID = 0x01
ARTOO_STORED_PARAMS_ID = 0x0B

class Axis():
    def __init__(self):
        self.min = sys.maxsize
        self.trim = 0
        self.max = 0
        self.current = 0

    def update(self, val):
        self.min = min(self.min, val)
        self.max = max(self.max, val)
        self.current = val

    def extremes(self):
        return "%04d:%04d:%04d" % (self.min, self.trim, self.max)
    
    def vals(self):
        return [self.min, self.trim, self.max]

    def latch_trim(self):
        self.trim = self.current

def get_and_print_sysinfo(slipdev):
    while True:
        slipdev.write(bytes([ARTOO_SYSINFO_ID]))
        pkt = slipdev.read()
        if pkt[0] == ARTOO_SYSINFO_ID:
            uid = pkt[:12].hex()
            hw_version = struct.unpack_from('<H', pkt, 12)[0]
            sw_version = pkt[14:].decode("utf-8", errors="replace")
            print(
                "calibrating artoo (UID: %s, hw version %d, sw version %s)"
                % (uid, hw_version, sw_version)
            )
            return

def wait_for_params(slipdev):
    while True:
        slipdev.write(bytes([ARTOO_STORED_PARAMS_ID]))
        pkt = slipdev.read()
        if pkt[0] == ARTOO_STORED_PARAMS_ID:
            return pkt[1:]


def mainloop(serialpath):

    serialport = serial.Serial(serialpath, ARTOO_BAUD)
    slipdev = slip.SlipDevice(serialport)

    axes = [Axis() for n in range(6)]
    get_and_print_sysinfo(slipdev)

    # enter raw io mode
    slipdev.write(bytes([ARTOO_RAW_IO_ID, 1]))

    try:
        while True:
            pkt = slipdev.read()
            if pkt[0] == ARTOO_RAW_IO_REPORT_ID:
                payload = pkt[1:]
                # payload consists of 16-bit values - extract them
                # v0.3.4 and above reports an additional raw io (battery id value)
                halfwords = len(payload) // 2
                fmtstr = "<" + ("H" * halfwords)
                vals = struct.unpack(fmtstr, payload)
                for i, v in enumerate(vals[:len(axes)]):
                    axes[i].update(v)

                sys.stdout.write(("Zero: %s, One %s, Two %s, Three %s cam pitch %s, cam rate %s\r") % (axes[0].extremes(), axes[1].extremes(),axes[2].extremes(), axes[3].extremes(), axes[4].extremes(), axes[5].extremes()))
                sys.stdout.flush()

    except KeyboardInterrupt:
        pass

    vals = []
    for a in axes:
        a.latch_trim()
        vals.extend(a.vals())
    packed_vals = struct.pack("<HHHHHHHHHHHHHHHHHH", *vals)
    slipdev.write(bytes([ARTOO_CALIBRATE_ID]) + packed_vals)

    print(
        "Stored: Zero: %s, One %s, Two %s, Three %s cam pitch %s, cam rate %s\r"
        % (
            axes[0].extremes(),
            axes[1].extremes(),
            axes[2].extremes(),
            axes[3].extremes(),
            axes[4].extremes(),
            axes[5].extremes(),
        )
    )

    print("\nverifying calibration...")
    params = wait_for_params(slipdev)
    # XXX: compare params...
    print("Calibration complete, values stored.")


#
# main
#

if len(sys.argv) < 2:
    print("usage: stick-cal.py /dev/ttyMyDev")
    sys.exit(1)

mainloop(sys.argv[1])
