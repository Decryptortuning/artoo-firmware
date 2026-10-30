#!/bin/sh
# GDB load has no coordinated storage-layout or preservation contract.
echo "Direct flashing retired; use the guarded journal-aware updater after hardware approval." >&2
exit 1
