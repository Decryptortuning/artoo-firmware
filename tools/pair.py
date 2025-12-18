#!/usr/bin/env python3

import sys, os, struct, datetime
import serial, slip

ARTOO_BAUD = 115200
SOLO_NAME = "SOLOLINK_12345"
SOLO_NAME_LONG = "mock vehicle with a name that is just waaaaaaaaaaaaaaay too lonnnnnnnnggggggggggg"

ARTOO_SYSINFO_ID = 0x03
ARTOO_PAIR_REQUEST_ID = 0x07
ARTOO_PAIR_CONFIRM_ID = 0x08
ARTOO_PAIR_RESULT_ID = 0x09
ARTOO_STORED_PARAMS_ID = 0x0B

def mainloop(serialpath):

    serialport = serial.Serial(serialpath, ARTOO_BAUD)
    slipdev = slip.SlipDevice(serialport)

    msg = bytes([ARTOO_PAIR_REQUEST_ID]) + SOLO_NAME.encode("utf-8") + b"\0"
    slipdev.write(msg)

    t = datetime.datetime.now()

    while True:
        pkt = slipdev.read()
        if pkt[0] == ARTOO_PAIR_CONFIRM_ID:
            print("pair confirmed", len(pkt[1:]), pkt[1:])
            slipdev.write(bytes([ARTOO_PAIR_RESULT_ID]) + pkt[1:])
            break

        dt = datetime.datetime.now() - t
        if dt.seconds > 10:
            print("timeout")
            slipdev.write(bytes([ARTOO_PAIR_RESULT_ID, 0x00]))
            break

    while True:
        slipdev.write(bytes([ARTOO_STORED_PARAMS_ID]))
        pkt = slipdev.read()
        if pkt[0] == ARTOO_STORED_PARAMS_ID:
            break

    print("pairing complete.")

#
# main
#

if len(sys.argv) < 2:
    print("usage: pair.py /dev/ttyMyDev")
    sys.exit(1)

mainloop(sys.argv[1])
