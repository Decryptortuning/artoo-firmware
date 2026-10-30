# Read-only runtime diagnostics

Host ID27 request is exactly schema byte1 followed by a two-byte little-endian
request token. The firmware accepts at most one per second, holds one pending
reply and produces it after ordinary control/status producers. It does not set
host-connected state, clear counters/records, change outputs or write parameters.
A busy or rate-limited request is dropped. The Linux bridge remains sole UART owner.

Reply payload is exactly204 bytes, excluding ID27. All integers are explicitly
little endian. Offset0 schema1;1 flags(bit0 valid fault,bit1 bench instrumentation);
2 length uint16=204;4 echoed token uint16;6 reserved uint16=0;8 reset RCC.CSR uint32;
12 fault record16uint32;76 metrics32uint32. Invalid retained records encode all zeros.
Unknown schema, lengths, flag bits and reserved fields should be rejected by peers.

Fault words: magic0x41525446,format0x00010040,IPSR,EXC_RETURN,rawSP,stackedPC,
stackedLR,xPSR,CFSR,HFSR,MMFAR,BFAR,currentTask,flags,reserved0,checksum. Flag bit0
means the exception frame passed range/alignment/EXC_RETURN/stacking-error checks.
MMFAR/BFAR are zero unless their valid status bit is set. Checksum is FNV-1a32 over
little-endian bytes of words1 through14. Record magic commits last. Current task
is0xffffffff outside foreground task dispatch. The handler uses a separate512byte
aligned emergency stack and masks ordinary interrupts; nested entry halts without
reusing that stack. No formatting, allocation, normal task locks, serial/flash
operations, automatic reset or GPIO changes occur. A later reset can retain RAM;
power-loss retention is not promised. `.noinit` is outside data/BSS startup ranges
and the heap. Backup-domain registers and their existing reset policy are untouched.

Metrics0..8 are UART txBytes,rxBytes,malformedBytes,overruns,dropped,lineErrors,
rejectedFrames,txRejected,txErrors. These remain available in production.
Bench-only metrics9..16 are stackUsed,heapUsed,heapPeak,OOM,heartbeatPosted,
heartbeatAccounted,heartbeatCoalesced,maxForegroundLoopGapTicks;17..23 task counts;
24..30 maximum task durations;31 reserved0. Task indices match Tasks::TaskID0..6.
32bit counters wrap; durations saturate at uint32 max and use9MHz SysTime ticks.
Heartbeat accounted is the posted sequence sampled when heartbeat work runs;
coalescing counts excess posted work, not missed physical SysTicks. SysTime cannot
recover multiple hardware periods collapsed into one pending interrupt.

Bench instrumentation uses existing ARTOO_BENCH_NO_IDLE selection; factory and
production encode these metrics zero. Stack painting only covers the reserved
4KiB stack area below the current MSP with a64byte guard; the untouched startup
frame contributes conservative usage. It does not measure prior startup peak,
whole-program worst-case depth, fault-stack depth or physical interrupt nesting.
Heap accounting reuses the existing bounded _sbrk guard. Timing/stack scans are
bench observations, not physical timing qualification. Snapshots are bounded;
UART and bench counters each have their own short coherent capture, not one
simultaneous cross-domain timestamp.

Validation entry points: tests/check_diagnostics.py, tests/check_image.py and the
integration Renode harness. Freeze combined firmware, Linux consumer and checker
sources before executing qualification. Physical fault/reset retention and control
latency require separately authorized hardware acceptance.
