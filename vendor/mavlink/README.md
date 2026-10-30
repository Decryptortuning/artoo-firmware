# Owned Solo MAVLink export

The XML authority remains meta-3dr/recipes-mavlink/mavlink/files/solo-dialect.
contract.json pins that XML closure, the product patch and PyMAVLink2.4.50 source
archive. No duplicate XML or new generated headers are checked into this repo.
The old src/mavlink/c_library tree is retained solely for comparison during this
migration; production includes select the explicit generated Solo export.

After the combined source freeze, using a Python3.12 environment with lxml:

```
python3 tools/export_mavlink.py --archive /path/pymavlink-2.4.50.tar.gz \
  --product-root /home/jresp/solo/workspace/layers/meta-3dr \
  --epoch 1793318400 --output /path/new-immutable-export
python3 tests/check_mavlink_contract.py --mavlink-export /path/new-immutable-export \
  --contract /home/jresp/Active_GH/OPenSolo_2/tests/data/solo-mavlink-v1-contract.json \
  --report /path/contract-report.json
python3 tests/check_mavlink.py --mavlink-export /path/new-immutable-export
```

CMake ARTOO_MAVLINK_EXPORT selects the export root. manifest.json records a
header_sha256 map relative to that root, all input hashes, exporter identity,
Python/lxml versions and epoch. Incomplete generation has no manifest. The
export directory must not exist before generation. Header generation does not
need an installed PyMAVLink package or network access. Use the complete existing
product patch to retain generator date determinism; only C1 generation runs.
No MAVLink2 wire, message numbering, command retry or vehicle capability changes
are part of this update. Byte-level CRC and consumer/UART regression checks are
still required in addition to static field assertions.

The retained library differs in two unused remote-logging messages: old141/142
become the existing Linux Solo184/185, with reviewed field and CRC differences;
MAV_COMP_ID_LOG changes50 to155 and four remote-log enums are added. Runtime
Artoo sources have no REMOTE_LOG/MAV_COMP_ID_LOG consumer. The checker asserts
exact old definitions from tests/legacy_mavlink_differences.json and the full
new166-message contract, plus absence of those identifiers in runtime sources.
It constructs166 independent golden frames and CRCs, checks actual parser/
reserializer byte equality and damaged-CRC recovery under UBSan.

The owned0001-c1-aligned-checksum.patch repairs upstream C1 helpers to accumulate
through an aligned local uint16_t instead of taking a packed-member address.
It is applied only to temporary extracted generator input, is hash-pinned in
contract.json, and does not edit the product patch or vendor source checkout.
The deliberate unaligned heartbeat/start/update and finalize-buffer tests retain
packed-member warnings as errors. No warning suppression is used.
