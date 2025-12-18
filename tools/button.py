#
# helpful defs for manipulating artoo button functions
#


import sys, os, struct

ARTOO_STORED_PARAMS_ID = 0x0B
ARTOO_BUTTON_FUNC_CFG_ID = 0x10

# button IDs
Power = 0
Fly = 1
RTL = 2
Loiter = 3
A = 4
B = 5
Preset1 = 6
Preset2 = 7
CameraClick = 8

# button events
Press =  0
Release = 1
ClickRelease = 2
Hold = 3
LongHold = 4
DoubleClick = 5

# bit positions for 'state' below
Enabled     = (1 << 0)
Hilighted	= (1 << 1)

# button names
ButtonName = ["Power", "Fly", "RTL", "Loiter", "A", "B",
              "Preset1", "Preset2", "CameraClick"]

def pack(descriptor, shot, btn_id, btn_evt, state=Enabled):
    if isinstance(descriptor, str):
        descriptor_bytes = descriptor.encode("utf-8")
    else:
        descriptor_bytes = descriptor
    return struct.pack(
        "<BBbb%ds" % (len(descriptor_bytes) + 1),
        btn_id,
        btn_evt,
        shot,
        state,
        descriptor_bytes,
    )
