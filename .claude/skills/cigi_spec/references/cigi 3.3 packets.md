# CIGI 3.3 Packet Catalog

Authoritative reference for CIGI Version 3.3 packet layouts. Populated
verbatim from the official Boeing/SISO ICD shipped under
`source/CIGI_ICD_3_3.pdf` (TST08I016, Issue Date 3 November 2008,
authors L. Durham + B. Phelps, The Boeing Company). Errata applied from
`source/CIGI_3_3_Appendix_C.pdf` and noted under each affected packet.

## Source mapping

- ICD PDF: `.claude/skills/cigi_spec/references/source/CIGI_ICD_3_3.pdf` (279 PDF pages)
- Errata PDF: `.claude/skills/cigi_spec/references/source/CIGI_3_3_Appendix_C.pdf`
- **PDF page = document page + 12** (12 pages of front matter)
- Section 4 starts at document page 38 = PDF page 50
- Each packet entry below cites its document section (`§4.1.N` / `§4.2.N`)

When verifying an entry, open the PDF at the cited page and compare offsets
byte-for-byte. The catalog never paraphrases offsets, sizes, or field
names — discrepancies between this file and the PDF mean this file is
wrong.

## Spec conventions (apply to every packet)

These are extracted from §2 *Interface Theory* of the ICD. Read once; then
the packet entries below assume them.

### Message structure (§2.6)

- A CIGI **message** = one or more packets concatenated in a single UDP
  datagram. Receiver walks packet-by-packet using the `Packet Size` field.
- First two bytes of every packet:
  - byte 0 = **Packet ID** (8-bit opcode — see Table 1 below)
  - byte 1 = **Packet Size** (8-bit total packet length in bytes,
    including header)
- The first 8 bytes of every packet contain enough data to uniquely
  identify the object the packet refers to (entity ID, view ID, etc.).
- 16-bit data must begin on 16-bit boundaries, 32-bit on 32-bit, 64-bit on
  64-bit boundaries. **Every packet must begin and end on a 64-bit
  boundary** — packets are padded so total size is a multiple of 8 bytes.
- First packet in a Host→IG datagram **must** be `IG Control` (§4.1.1).
- First packet in an IG→Host datagram **must** be `Start of Frame` (§4.2.1).
- An object must be **created before it is referenced**. E.g. a Component
  Control referring to entity *n* must come *after* the Entity Control
  that instantiates *n*, in the same or an earlier message. Within one
  message: ordering matters.

### Packet ID space (Table 1, §2.6)

| Range          | Direction | Reserved for                                                                 |
|----------------|-----------|------------------------------------------------------------------------------|
| `0x01`–`0x23`  | Host → IG | 35 standard packets (`IG Control` … `Short Symbol Control`)                  |
| `0x65`–`0x75`  | IG → Host | 17 standard packets (`Start of Frame` … `Image Generator Message`)           |
| `0xC9`–`0xFF`  | Either    | User-defined data packets (opcodes 201–255). See §4.3.                       |

Opcodes outside these ranges are **reserved** — devices must ignore
unrecognised packet IDs silently (forward-compatibility rule, §2.11).

### Byte order (§2.5)

- **CIGI 3 does not impose a byte order.** Sender writes in its native
  byte order; receiver byte-swaps if necessary.
- The `IG Control` and `Start of Frame` packets each carry a 16-bit
  **Byte Swap Magic Number** field. The sender writes `0x8000` (high byte
  bit 7 = 1, low byte = 0). The receiver inspects:
  - if the *low* byte's MSB is set → bytes were swapped → swap whole
    message
  - if the *high* byte's MSB is set → no swap needed
- For interoperability with most deployed IGs, **send big-endian**
  (network byte order) — that's what virtually every Host implementation
  uses, including this codebase (`cigi_host_node.cpp`).

### Data types (§2.7, Table 2)

| Type          | Bytes | Range                                                       |
|---------------|-------|-------------------------------------------------------------|
| `octet`       | 1     | 0..255                                                      |
| `int8`        | 1     | -128..127                                                   |
| `unsigned int8` | 1   | 0..255                                                      |
| `int16`       | 2     | -32 768..32 767                                             |
| `unsigned int16`| 2   | 0..65 535                                                   |
| `int32`       | 4     | ±2.147e9                                                    |
| `unsigned int32`| 4   | 0..4.295e9                                                  |
| `word`        | 4     | unsigned int32 used as generic 32-bit datum                 |
| `single float`| 4     | IEEE-754 single (~7 digits precision)                       |
| `double float`| 8     | IEEE-754 double (~15 digits precision)                      |

UTF-8 strings (§2.7.2): variable-length, 1–4 octets per code point.
Implementations must not split a multi-byte code point when truncating
to fit a packet.

### Bit fields (§2.7.3)

- Bit numbering within a byte: **bit 0 = LSB**, bit 7 = MSB.
- Multiple bit fields are packed starting at the LSB.
- Bit fields **never cross byte boundaries** in CIGI 3.
- Bits marked *Reserved* must be set to 0 by the sender.
- A receiver must accept a packet whose `Packet Size` is *larger* than
  expected — extra bytes are ignored. This is the forward-compatibility
  hook.

### Frame numbering (§2.3)

- Each direction maintains its own monotonically incrementing frame
  number.
- Host writes `Host Frame Number` in IG Control. IG echoes it back as
  `Last Host Frame Number` in the next SOF.
- IG writes `IG Frame Number` in SOF. Host echoes it back as
  `Last IG Frame Number` in the next IG Control.
- The Host can compute round-trip latency from the gap between its
  current frame number and the value the IG most recently echoed.

### IG modes (§2.8)

`Start of Frame` carries an `IG Mode` field with values:

| Value | Mode               | Meaning                                                |
|-------|--------------------|--------------------------------------------------------|
| 0     | Reset / Standby    | Initialising, database load, or post-shutdown reset    |
| 1     | Operate            | Mission-ready; Host may send any packet                |
| 2     | Debug              | Vendor-defined diagnostic mode                         |
| 3     | Offline Maintenance| Not commandable via CIGI; reported only                |

Host transitions IG via the `IG Mode` field of `IG Control`; IG reports
its current mode back in SOF.

### Packet summary (Table 1, §2.6)

Mandatory column lists which packets must appear in *every* message in
synchronous mode.

**Host → IG (§4.1)**

| Opcode | Hex   | Packet                                       | Mandatory each frame |
|--------|-------|----------------------------------------------|----------------------|
| 1      | 0x01  | IG Control                                   | **Yes**              |
| 2      | 0x02  | Entity Control                               | No                   |
| 3      | 0x03  | Conformal Clamped Entity Control             | No                   |
| 4      | 0x04  | Component Control                            | No                   |
| 5      | 0x05  | Short Component Control                      | No                   |
| 6      | 0x06  | Articulated Part Control                     | No                   |
| 7      | 0x07  | Short Articulated Part Control               | No                   |
| 8      | 0x08  | Rate Control                                 | No                   |
| 9      | 0x09  | Celestial Sphere Control                     | No                   |
| 10     | 0x0A  | Atmosphere Control                           | No                   |
| 11     | 0x0B  | Environmental Region Control                 | No                   |
| 12     | 0x0C  | Weather Control                              | No                   |
| 13     | 0x0D  | Maritime Surface Conditions Control          | No                   |
| 14     | 0x0E  | Wave Control                                 | No                   |
| 15     | 0x0F  | Terrestrial Surface Conditions Control       | No                   |
| 16     | 0x10  | View Control                                 | No                   |
| 17     | 0x11  | Sensor Control                               | No                   |
| 18     | 0x12  | Motion Tracker Control                       | No                   |
| 19     | 0x13  | Earth Reference Model Definition             | No                   |
| 20     | 0x14  | Trajectory Definition                        | No                   |
| 21     | 0x15  | View Definition                              | No                   |
| 22     | 0x16  | Collision Detection Segment Definition       | No                   |
| 23     | 0x17  | Collision Detection Volume Definition        | No                   |
| 24     | 0x18  | HAT/HOT Request                              | No                   |
| 25     | 0x19  | Line of Sight Segment Request                | No                   |
| 26     | 0x1A  | Line of Sight Vector Request                 | No                   |
| 27     | 0x1B  | Position Request                             | No                   |
| 28     | 0x1C  | Environmental Conditions Request             | No                   |
| 29     | 0x1D  | Symbol Surface Definition                    | No                   |
| 30     | 0x1E  | Symbol Text Definition                       | No                   |
| 31     | 0x1F  | Symbol Circle Definition                     | No                   |
| 32     | 0x20  | Symbol Line Definition                       | No                   |
| 33     | 0x21  | Symbol Clone                                 | No                   |
| 34     | 0x22  | Symbol Control                               | No                   |
| 35     | 0x23  | Short Symbol Control                         | No                   |

**IG → Host (§4.2)**

| Opcode | Hex   | Packet                                       | Mandatory each frame |
|--------|-------|----------------------------------------------|----------------------|
| 101    | 0x65  | Start of Frame                               | **Yes**              |
| 102    | 0x66  | HAT/HOT Response                             | No                   |
| 103    | 0x67  | HAT/HOT Extended Response                    | No                   |
| 104    | 0x68  | Line of Sight Response                       | No                   |
| 105    | 0x69  | Line of Sight Extended Response              | No                   |
| 106    | 0x6A  | Sensor Response                              | See packet description |
| 107    | 0x6B  | Sensor Extended Response                     | See packet description |
| 108    | 0x6C  | Position Response                            | No                   |
| 109    | 0x6D  | Weather Conditions Response                  | No                   |
| 110    | 0x6E  | Aerosol Concentration Response               | No                   |
| 111    | 0x6F  | Maritime Surface Conditions Response         | No                   |
| 112    | 0x70  | Terrestrial Surface Conditions Response      | No                   |
| 113    | 0x71  | Collision Detection Segment Notification     | No                   |
| 114    | 0x72  | Collision Detection Volume Notification      | No                   |
| 115    | 0x73  | Animation Stop Notification                  | No                   |
| 116    | 0x74  | Event Notification                           | No                   |
| 117    | 0x75  | Image Generator Message                      | No                   |

**User-defined (§4.3):** opcodes 201–255 (`0xC9`–`0xFF`).

## Notes for this codebase

These are deviations or quirks specific to `simulator_framework`'s
current CIGI integration. They do **not** describe the standard — they
warn about places where standard 3.3 and our wire reality differ.

1. **Non-standard IG→Host packet IDs in `cigi_host_node.cpp`.** As of
   commit `880c490`, `src/core/cigi_bridge/src/cigi_host_node.cpp`
   parses incoming SOF as packet ID `0x01` (lines 643–656) and HAT/HOT
   Response as packet ID `0x02` (lines 570–642). The accompanying
   comments label these as "Standard CIGI 3.3", but standard SOF is
   `0x65` and HAT/HOT Response is `0x66`.

   Possible explanations:
   - the X-Plane CIGI plugin (`x-plane_plugins/xplanecigi/`) emits
     non-standard IDs that the host node is matched against, or
   - the host node was written against a misread of the spec.

   When integrating a third-party IG (any IG that uses the CCL or any
   conformant CIGI 3.3 stack), the host node will need to accept the
   standard `0x65` / `0x66` IDs as well. Verify against the actual
   wire traffic before assuming either set.

2. **Raw encoder, no CCL on the host side.** `cigi_host_node.cpp` writes
   IG Control, Entity Control, HAT/HOT Request and weather packets
   directly with `htons` / `htonll`-equivalent byte assembly. There is
   no Byte Swap Magic Number negotiation: the host always writes
   big-endian, and the IG is assumed to interpret big-endian. Conformant
   CIGI 3.3 IGs follow the magic-number rule, so this works in practice
   on big-endian-friendly receivers but is not strictly compliant.

3. **Custom HAT/HOT extended response (48 bytes).** Our X-Plane plugin
   sends a 48-byte response that prepends standard fields and appends
   `surface_type` (octet @24), `static_friction` (single float @28),
   `rolling_friction` (single float @32). Standard CIGI 3.3 HAT/HOT
   Extended Response is 40 bytes (see §4.2.3 / packet `0x67` below) and
   has no friction fields.

4. **CCL-based vendored layer.** `src/core/cigi_bridge/cigi_ig_interface/`
   uses standard CCL types (`CigiIGCtrlV3_3`, `CigiBaseEventProcessor`).
   This layer compiles only when CCL is installed — see `CMakeLists.txt`.

---

## Template (used per packet entry below)

```
### 0xNN — Packet Name

- **Direction**: Host→IG | IG→Host
- **Opcode**: NN (decimal) / 0xNN
- **Total size**: NN bytes
- **ICD section**: §4.X.N (PDF page YY)
- **Mandatory each frame**: yes | no | conditional

**Fields**

| Offset | Size | Name              | Type    | Range / Values        | Notes |
|--------|------|-------------------|---------|------------------------|-------|
| 0      | 1    | Packet ID         | uint8   | 0xNN                   | fixed |
| 1      | 1    | Packet Size       | uint8   | NN                     | fixed |
| ...    | ...  | ...               | ...     | ...                    | ...   |

**Usage notes**
- frequency
- dependencies
- common pitfalls
```

---

## Host → IG packets

### 0x01 — IG Control

- **Direction**: Host → IG
- **Opcode**: 1 / 0x01
- **Total size**: 24 bytes
- **ICD section**: §4.1.1 (PDF page 53)
- **Mandatory each frame**: **yes** (every Host→IG datagram must start
  with exactly one IG Control packet)

Controls the IG's operational mode, database loading and frame timing.

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                              | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 1                                                           | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 24                                                          | fixed |
| 2      | 1    | Major Version         | unsigned int8   | 3                                                           | Host must set to 3 |
| 3      | 1    | Database Number       | int8            | 0 = no load; 1..127 = desired DB; negative = IG is loading  | See §4.1.1 database-load handshake |
| 4      | —    | Minor Version         | 4-bit field     | minor revision number                                        | byte 4, bits 7..4 (value 2 for CIGI 3.3) |
| 4      | —    | Extrapolation/Interpolation Enable (\*3) | 1-bit | 0 Disable / 1 Enable                                | byte 4, bit 3. Global gate for per-entity smoothing |
| 4      | —    | Timestamp Valid (\*2) | 1-bit           | 0 Invalid / 1 Valid                                          | byte 4, bit 2. Must be Valid in async mode |
| 4      | —    | IG Mode (\*1)         | unsigned 2-bit  | 0 Reset/Standby, 1 Operate, 2 Debug                          | byte 4, bits 1..0 |
| 5      | 1    | Reserved              | —               | 0                                                            | sender must zero |
| 6      | 2    | Byte Swap Magic Number| unsigned int16  | **0x8000** (sender always writes this)                       | receiver checks for 0x80 in low byte to know if it must swap |
| 8      | 4    | Host Frame Number     | unsigned int32  | monotonic                                                    | incremented by 1 each frame |
| 12     | 4    | Timestamp             | unsigned int32  | 10 µs ticks since arbitrary reference                        | required in async; rollover ~12 h |
| 16     | 4    | Last IG Frame Number  | unsigned int32  | echo of last received SOF `IG Frame Number`                  | latency measurement |
| 20     | 4    | Reserved              | —               | 0                                                            | padding to 8-byte boundary |

**Usage notes**
- Sent every frame; every Host→IG message must start with exactly one
  IG Control. More than one in a frame → undefined IG behaviour.
- Database load handshake (§4.1.1): Host sets `Database Number` = `+n`;
  IG echoes `-n` in SOF while loading; Host then sets `Database Number`
  = 0 to acknowledge; IG signals load complete by setting SOF
  `Database Number` = `+n` again.
- Pre-CIGI-3.2 devices used a single `Frame Counter` field instead of
  separate `Host Frame Number` / `Last IG Frame Number`.

### 0x02 — Entity Control

- **Direction**: Host → IG
- **Opcode**: 2 / 0x02
- **Total size**: 48 bytes
- **ICD section**: §4.1.2 (PDF page 57)
- **Mandatory each frame**: no — send only when entity state changes,
  or every frame for entities that move continuously (Ownship, traffic).

Creates, controls and destroys entities. Same packet drives the Ownship
(Entity ID = 0) and every other entity in the scene.

**Fields**

| Offset | Size | Name                              | Type            | Range / Values                                              | Notes |
|-------:|-----:|-----------------------------------|-----------------|-------------------------------------------------------------|-------|
| 0      | 1    | Packet ID                         | unsigned int8   | 2                                                           | fixed |
| 1      | 1    | Packet Size                       | unsigned int8   | 48                                                          | fixed |
| 2      | 2    | Entity ID                         | unsigned int16  | 0 = Ownship; 1..65535 = other entities                      |       |
| 4      | —    | Entity State (\*1)                | unsigned 2-bit  | 0 Inactive/Standby, 1 Active, 2 Destroyed                   | byte 4, bits 1..0 |
| 4      | —    | Attach State (\*2)                | 1-bit           | 0 Detach, 1 Attach                                          | byte 4, bit 2 |
| 4      | —    | Collision Detection Enable (\*3)  | 1-bit           | 0 Disabled, 1 Enabled                                       | byte 4, bit 3 |
| 4      | —    | Inherit Alpha (\*4)               | 1-bit           | 0 Not Inherited, 1 Inherited                                | byte 4, bit 4 |
| 4      | —    | Ground/Ocean Clamp (\*5)          | unsigned 2-bit  | 0 No Clamp, 1 Non-Conformal, 2 Conformal                    | byte 4, bits 6..5 |
| 4      | —    | Reserved (\*6)                    | 1-bit           | 0                                                           | byte 4, bit 7 |
| 5      | —    | Animation Direction (\*7)         | 1-bit           | 0 Forward, 1 Backward                                       | byte 5, bit 0 |
| 5      | —    | Animation Loop Mode (\*8)         | 1-bit           | 0 One-Shot, 1 Continuous                                    | byte 5, bit 1 |
| 5      | —    | Animation State (\*9)             | unsigned 2-bit  | 0 Stop, 1 Pause, 2 Play, 3 Continue                         | byte 5, bits 3..2 |
| 5      | —    | Linear Extrapolation/Interpolation Enable (\*10) | 1-bit | 0 Disable, 1 Enable                              | byte 5, bit 4 |
| 5      | —    | Reserved                          | 3-bit           | 0                                                           | byte 5, bits 7..5 |
| 6      | 1    | Alpha                             | unsigned int8   | 0 fully transparent .. 255 fully opaque                     |       |
| 7      | 1    | Reserved                          | —               | 0                                                           | padding |
| 8      | 2    | Entity Type                       | unsigned int16  | IG-defined; 0 = null type (Ownship/free camera)             | invalid type → packet discarded |
| 10     | 2    | Parent ID                         | unsigned int16  | Entity ID of parent; ignored if Attach State = 0            |       |
| 12     | 4    | Roll                              | single float    | -180.0 .. +180.0 deg                                        | datum varies — see §4.1.2 Table 7 |
| 16     | 4    | Pitch                             | single float    | -90.0 .. +90.0 deg                                          |       |
| 20     | 4    | Yaw                               | single float    | 0.0 .. 360.0 deg                                            | from True North (top-level) or parent +X (child) |
| 24     | 8    | Latitude (top-level) / X Offset (child) | double float | -90 .. +90 deg / metres                                | meaning controlled by Attach State |
| 32     | 8    | Longitude (top-level) / Y Offset (child) | double float | -180 .. +180 deg / metres                              |       |
| 40     | 8    | Altitude (top-level) / Z Offset (child) | double float | metres MSL or metres above terrain (per Ground/Ocean Clamp) |       |

**Usage notes**
- First Entity Control for a new ID *creates* the entity; subsequent
  ones update it. Setting `Entity State` = Destroyed unloads the entity
  and all its children.
- An entity must exist before any other packet (Component Control,
  Articulated Part, Rate Control, etc.) can reference it.
- Position semantics flip on `Attach State`: top-level → geodetic
  lat/lon/alt; child → metric X/Y/Z in parent's body frame.
- `Ground/Ocean Clamp` = Conformal (2) makes the IG own pitch/roll
  conformity to terrain; pitch/roll then specify offsets, not absolute
  attitudes. For a lighter-weight conformal update without the full
  48 bytes, use `Conformal Clamped Entity Control` (§4.1.3).

### 0x03 — Conformal Clamped Entity Control

- **Direction**: Host → IG
- **Opcode**: 3 / 0x03
- **Total size**: 24 bytes
- **ICD section**: §4.1.3 (PDF page 68)
- **Mandatory each frame**: no

Lightweight position update for entities already created via Entity
Control with `Ground/Ocean Clamp` = Conformal. Saves bandwidth on
ground vehicles, parked aircraft, etc. that conform to terrain — only
heading and 2-D position need to be retransmitted; roll/pitch/altitude
are owned by the IG.

**Fields**

| Offset | Size | Name        | Type            | Range / Values                  | Notes |
|-------:|-----:|-------------|-----------------|---------------------------------|-------|
| 0      | 1    | Packet ID   | unsigned int8   | 3                               | fixed |
| 1      | 1    | Packet Size | unsigned int8   | 24                              | fixed |
| 2      | 2    | Entity ID   | unsigned int16  | 0 = Ownship                     | must already exist with Conformal clamping |
| 4      | 4    | Yaw         | single float    | 0.0 .. 360.0 deg, datum True N  |       |
| 8      | 8    | Latitude    | double float    | -90 .. +90 deg                  |       |
| 16     | 8    | Longitude   | double float    | -180 .. +180 deg                |       |

**Usage notes**
- Cannot create entities — first Entity Control packet must instantiate
  the entity and set `Ground/Ocean Clamp` = Conformal (2).
- Sent against an unclamped or non-conformal entity → packet ignored;
  the entity keeps its current absolute roll/pitch/altitude.
- The IG re-derives roll, pitch, and altitude each frame from terrain
  slope at the new lat/lon.

### 0x04 — Component Control

- **Direction**: Host → IG
- **Opcode**: 4 / 0x04
- **Total size**: 32 bytes
- **ICD section**: §4.1.4 (PDF page 70)
- **Mandatory each frame**: no

Generic mechanism to control any IG-side object that does not have a
dedicated CIGI packet (entity sub-features, view zoom, weather layer
parameters, runway lights, sensor gates, symbol attributes, …). The
host and IG must agree on a Component-ID-to-meaning mapping per
component class — see Table 9 in §4.1.4 for the standard examples.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                             | Notes |
|-------:|-----:|-------------------|-----------------|------------------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 4                                                          | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 32                                                         | fixed |
| 2      | 2    | Component ID      | unsigned int16  | IG-defined; for Regional Layered Weather, MS byte = layer  |       |
| 4      | 2    | Instance ID       | unsigned int16  | Entity ID / View ID / Region ID / etc. — see Table 9       | ignored for Global classes |
| 6      | —    | Component Class   | unsigned 6-bit  | 0..15 defined (Entity..Symbol); 16..63 Reserved            | byte 6, bits 5..0 |
| 6      | —    | Reserved          | 2-bit           | 0                                                          | byte 6, bits 7..6 |
| 7      | 1    | Component State   | unsigned int8   | component-specific discrete state                          |       |
| 8      | 4    | Component Data 1  | word            | user-defined 32-bit datum                                  | byte-swapped as 32-bit |
| 12     | 4    | Component Data 2  | word            | user-defined                                               |       |
| 16     | 4    | Component Data 3  | word            | user-defined                                               |       |
| 20     | 4    | Component Data 4  | word            | user-defined                                               |       |
| 24     | 4    | Component Data 5  | word            | user-defined                                               |       |
| 28     | 4    | Component Data 6  | word            | user-defined                                               |       |

**Component Class enum (byte 6, bits 5..0)**

| Value | Class                  | Instance ID corresponds to                |
|------:|------------------------|-------------------------------------------|
| 0     | Entity                 | Entity ID                                 |
| 1     | View                   | View ID                                   |
| 2     | View Group             | View Group ID                             |
| 3     | Sensor                 | Sensor ID                                 |
| 4     | Regional Sea Surface   | Region ID                                 |
| 5     | Regional Terrain Surface | Region ID                               |
| 6     | Regional Layered Weather | Region ID (layer = MSB of Component ID) |
| 7     | Global Sea Surface     | — (Instance ID ignored)                   |
| 8     | Global Terrain Surface | —                                         |
| 9     | Global Layered Weather | Layer ID                                  |
| 10    | Atmosphere             | —                                         |
| 11    | Celestial Sphere       | —                                         |
| 12    | Event                  | Event ID                                  |
| 13    | System                 | —                                         |
| 14    | Symbol Surface         | Symbol Surface ID                         |
| 15    | Symbol                 | Symbol ID                                 |
| 16-63 | Reserved               | —                                         |

**Usage notes**
- The six `Component Data` words are byte-swapped *as 32-bit values*.
  When packing 8- or 16-bit sub-fields into a word, build the word with
  bit-shifts so that swapping the 32-bit value preserves the logical
  packing — see Figures 44–46 in the ICD for the canonical example.
- For 64-bit doubles, split into MSW (`Component Data N`) and
  LSW (`Component Data N+1`).
- Any component controllable via Short Component Control (§4.1.5) can
  also be driven from this packet.

### 0x05 — Short Component Control

- **Direction**: Host → IG
- **Opcode**: 5 / 0x05
- **Total size**: 16 bytes
- **ICD section**: §4.1.5 (PDF page 79)
- **Mandatory each frame**: no

Narrow version of Component Control (§4.1.4) for components that need
at most two 32-bit user words. Half the packet size. Component Class
field is only 4 bits here (15 → reserved at 16), matching the standard
class list.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                         | Notes |
|-------:|-----:|-------------------|-----------------|--------------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 5                                                      | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 16                                                     | fixed |
| 2      | 2    | Component ID      | unsigned int16  | same semantics as Component Control                    |       |
| 4      | 2    | Instance ID       | unsigned int16  | Entity ID / View ID / Region ID / etc.                 |       |
| 6      | —    | Component Class   | unsigned 4-bit  | 0..15 (Entity..Symbol); 16..63 not encodable           | byte 6, bits 3..0 |
| 6      | —    | Reserved          | 4-bit           | 0                                                      | byte 6, bits 7..4 |
| 7      | 1    | Component State   | unsigned int8   | component-specific                                     |       |
| 8      | 4    | Component Data 1  | word            | user-defined 32-bit datum                              | byte-swapped as 32-bit |
| 12     | 4    | Component Data 2  | word            | user-defined                                           |       |

**Usage notes**
- Component ID / Instance ID / Component Class mappings are identical
  to Component Control. IGs typically implement this by copying the
  packet into a zero-padded 32-byte Component Control structure and
  routing both through the same handler.
- Cannot carry 64-bit doubles (needs 2 MSW/LSW words — use
  Component Control instead).

### 0x06 — Articulated Part Control

- **Direction**: Host → IG
- **Opcode**: 6 / 0x06
- **Total size**: 32 bytes
- **ICD section**: §4.1.6 (PDF page 82)
- **Mandatory each frame**: no — send when articulation moves

Manipulates a single articulated submodel of an entity (gear, flap,
turret, control surface) in up to 6 DoF. Position values are
**absolute**, not cumulative — the IG uses the values verbatim each
frame the packet arrives.

**Fields**

| Offset | Size | Name                         | Type            | Range / Values                  | Notes |
|-------:|-----:|------------------------------|-----------------|---------------------------------|-------|
| 0      | 1    | Packet ID                    | unsigned int8   | 6                               | fixed |
| 1      | 1    | Packet Size                  | unsigned int8   | 32                              | fixed |
| 2      | 2    | Entity ID                    | unsigned int16  | parent entity                   |       |
| 4      | 1    | Articulated Part ID          | unsigned int8   | submodel index within entity    |       |
| 5      | —    | Articulated Part Enable (\*1)| 1-bit           | 0 Disable / 1 Enable            | byte 5, bit 0. Removes/inserts submodel from scene |
| 5      | —    | X Offset Enable (\*2)        | 1-bit           | 0/1                             | byte 5, bit 1 |
| 5      | —    | Y Offset Enable (\*3)        | 1-bit           | 0/1                             | byte 5, bit 2 |
| 5      | —    | Z Offset Enable (\*4)        | 1-bit           | 0/1                             | byte 5, bit 3 |
| 5      | —    | Roll Enable (\*5)            | 1-bit           | 0/1                             | byte 5, bit 4 |
| 5      | —    | Pitch Enable (\*6)           | 1-bit           | 0/1                             | byte 5, bit 5 |
| 5      | —    | Yaw Enable (\*7)             | 1-bit           | 0/1                             | byte 5, bit 6 |
| 5      | —    | Reserved (\*8)               | 1-bit           | 0                               | byte 5, bit 7 |
| 6      | 2    | Reserved                     | —               | 0                               | padding |
| 8      | 4    | X Offset                     | single float    | metres along submodel +X        | model-dependent default |
| 12     | 4    | Y Offset                     | single float    | metres along submodel +Y        |       |
| 16     | 4    | Z Offset                     | single float    | metres along submodel +Z        |       |
| 20     | 4    | Roll                         | single float    | -180.0 .. +180.0 deg            | applied after yaw + pitch |
| 24     | 4    | Pitch                        | single float    | -90.0 .. +90.0 deg              | applied after yaw |
| 28     | 4    | Yaw                          | single float    | 0.0 .. 360.0 deg                | rotation about submodel +Z |

**Usage notes**
- DoF *Enable* flags gate which fields the IG honours; cleared values
  leave the part at its current position. This lets the Host send only
  the deltas without re-specifying static DoFs.
- Use Short Articulated Part Control (§4.1.7) for 1- or 2-DoF updates
  to save bandwidth.
- A `Rate Control` packet (§4.1.8) addressed to (Entity ID, Articulated
  Part ID) lets the IG extrapolate continuous motion between Articulated
  Part Control updates.

### 0x07 — Short Articulated Part Control

- **Direction**: Host → IG
- **Opcode**: 7 / 0x07
- **Total size**: 16 bytes
- **ICD section**: §4.1.7 (PDF page 87)
- **Mandatory each frame**: no

Half-size articulation packet that drives one or two DoFs of one or two
articulated parts on the same entity. For 3+ DoF use Articulated Part
Control (§4.1.6).

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                            | Notes |
|-------:|-----:|-------------------------------|-----------------|-----------------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 7                                                         | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 16                                                        | fixed |
| 2      | 2    | Entity ID                     | unsigned int16  | parent entity                                             |       |
| 4      | 1    | Articulated Part ID 1         | unsigned int8   | first submodel index                                      |       |
| 5      | 1    | Articulated Part ID 2         | unsigned int8   | second submodel index (may equal Part ID 1)               |       |
| 6      | —    | DOF Select 1 (\*1)            | 3-bit field     | 0 Not Used, 1 X, 2 Y, 3 Z, 4 Yaw, 5 Pitch, 6 Roll         | byte 6, bits 2..0 |
| 6      | —    | DOF Select 2 (\*2)            | 3-bit field     | same enum                                                 | byte 6, bits 5..3 |
| 6      | —    | Articulated Part Enable 1 (\*3) | 1-bit         | 0 Disable / 1 Enable                                      | byte 6, bit 6 |
| 6      | —    | Articulated Part Enable 2 (\*4) | 1-bit         | 0 Disable / 1 Enable                                      | byte 6, bit 7 |
| 7      | 1    | Reserved                      | —               | 0                                                         |       |
| 8      | 4    | DOF 1                         | single float    | metres (X/Y/Z) or degrees (Yaw/Pitch/Roll)                | meaning per DOF Select 1 |
| 12     | 4    | DOF 2                         | single float    | metres or degrees                                         | meaning per DOF Select 2 |

**Usage notes**
- DOF Select = `Not Used` (0) → corresponding DOF + Enable fields are
  ignored; the part keeps its current value.
- If `DOF Select 1` and `DOF Select 2` reference the *same* DOF on the
  *same* part, `DOF 2` wins (last-in-takes-priority).
- Frequent use case: gear extension / flap deflection, where the host
  drives one DOF per part each frame.

### 0x08 — Rate Control

- **Direction**: Host → IG
- **Opcode**: 8 / 0x08
- **Total size**: 32 bytes
- **ICD section**: §4.1.8 (PDF page 90)
- **Mandatory each frame**: no — issue once when rate changes

Specifies linear and angular rates the IG should use to extrapolate
position between Entity Control / Articulated Part Control updates.
Useful for predictably moving objects (rotating radar, orbiting traffic)
and for jitter-smoothing under asynchronous operation.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                            | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 8                                         | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 32                                        | fixed |
| 2      | 2    | Entity ID                     | unsigned int16  | target entity                             |       |
| 4      | 1    | Articulated Part ID           | unsigned int8   | only used if Apply to Articulated Part = 1 |      |
| 5      | —    | Apply to Articulated Part (\*1) | 1-bit         | 0 False (apply to entity), 1 True (apply to part) | byte 5, bit 0 |
| 5      | —    | Coordinate System (\*2)       | 1-bit           | 0 World/Parent, 1 Local                   | byte 5, bit 1. Ignored when applied to articulated part |
| 5      | —    | Reserved                      | 6-bit           | 0                                         | byte 5, bits 7..2 |
| 6      | 2    | Reserved                      | —               | 0                                         | padding |
| 8      | 4    | X Linear Rate                 | single float    | m/s along X                               |       |
| 12     | 4    | Y Linear Rate                 | single float    | m/s along Y                               |       |
| 16     | 4    | Z Linear Rate                 | single float    | m/s along Z                               |       |
| 20     | 4    | Roll Angular Rate             | single float    | deg/s about X (after yaw + pitch applied) |       |
| 24     | 4    | Pitch Angular Rate            | single float    | deg/s about Y (after yaw applied)         |       |
| 28     | 4    | Yaw Angular Rate              | single float    | deg/s about Z                             |       |

**Usage notes**
- Rates persist until replaced. Setting all six rates to zero stops the
  entity (or part) cleanly.
- For top-level entities with `Coordinate System` = World/Parent, X/Y
  trace a line above the geoid — a long-running rate without periodic
  Entity Control updates will drift below or above the surface.
- When the entity is destroyed, all rates applied to it are annulled.
- Pair with Entity Control each frame to provide both ground-truth pose
  and a forward-extrapolation hint.

### 0x09 — Celestial Sphere Control

- **Direction**: Host → IG
- **Opcode**: 9 / 0x09
- **Total size**: 16 bytes
- **ICD section**: §4.1.9 (PDF page 94)
- **Mandatory each frame**: no

Sets sky-model state: time of day, sun/moon/stars enables, ephemeris
mode, date.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                          | Notes |
|-------:|-----:|-------------------------------|-----------------|-----------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 9                                       | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 16                                      | fixed |
| 2      | 1    | Hour                          | unsigned int8   | 0..23 (UTC)                             |       |
| 3      | 1    | Minute                        | unsigned int8   | 0..59 (UTC)                             |       |
| 4      | —    | Ephemeris Model Enable (\*1)  | 1-bit           | 0 Disable (static TOD) / 1 Enable       | byte 4, bit 0. Default 1 |
| 4      | —    | Sun Enable (\*2)              | 1-bit           | 0/1                                     | byte 4, bit 1 |
| 4      | —    | Moon Enable (\*3)             | 1-bit           | 0/1                                     | byte 4, bit 2 |
| 4      | —    | Star Field Enable (\*4)       | 1-bit           | 0/1                                     | byte 4, bit 3 |
| 4      | —    | Date/Time Valid (\*5)         | 1-bit           | 0 Invalid (ignore Hour/Minute/Date) / 1 Valid | byte 4, bit 4 |
| 4      | —    | Reserved                      | 3-bit           | 0                                       | byte 4, bits 7..5 |
| 5      | 3    | Reserved                      | —               | 0                                       |       |
| 8      | 4    | Date                          | unsigned int32  | MMDDYYYY = month×1 000 000 + day×10 000 + year | 7- or 8-digit decimal |
| 12     | 4    | Star Field Intensity          | single float    | 0..100 %                                | ignored if Star Field Enable = 0 |

**Usage notes**
- When the host freezes the sim, send a packet with `Ephemeris Model
  Enable` = 0 to stop continuous TOD update; re-enable on resume.

### 0x0A — Atmosphere Control

- **Direction**: Host → IG
- **Opcode**: 10 / 0x0A
- **Total size**: 32 bytes
- **ICD section**: §4.1.10 (PDF page 97)
- **Mandatory each frame**: no

Sets the *global* baseline atmosphere — temperature, humidity, pressure,
visibility, wind. Weather layers (§4.1.12) and weather entities override
this within their footprints; the global values still drive the
transition bands.

**Fields**

| Offset | Size | Name                            | Type            | Range / Values                              | Notes |
|-------:|-----:|---------------------------------|-----------------|---------------------------------------------|-------|
| 0      | 1    | Packet ID                       | unsigned int8   | 10                                          | fixed |
| 1      | 1    | Packet Size                     | unsigned int8   | 32                                          | fixed |
| 2      | 1    | Reserved                        | —               | 0                                           |       |
| 3      | —    | Atmospheric Model Enable (\*1)  | 1-bit           | 0 Disable / 1 Enable                        | byte 3, bit 0. Default 0. Enables FASCODE/MODTRAN/SEDRIS-style sensor-spectral models |
| 3      | —    | Reserved                        | 7-bit           | 0                                           | byte 3, bits 7..1 |
| 4      | 1    | Global Humidity                 | unsigned int8   | 0..100 %                                    |       |
| 5      | 3    | (padding before float)          | —               | —                                           | aligns next double / float on 8-byte boundary — Reserved |
| 8      | 4    | Global Air Temperature          | single float    | °C                                          |       |
| 12     | 4    | Global Visibility Range         | single float    | metres, ≥ 0                                 |       |
| 16     | 4    | Global Horizontal Wind Speed    | single float    | m/s, ≥ 0                                    | direction in `Global Wind Direction` |
| 20     | 4    | Global Vertical Wind Speed      | single float    | m/s; +ve = updraft, -ve = downdraft         |       |
| 24     | 4    | Global Wind Direction           | single float    | 0..360 deg from True North (wind FROM)      |       |
| 28     | 4    | Global Barometric Pressure      | single float    | mb / hPa, ≥ 0                               |       |

> **Note on padding** — the ICD figure shows `Reserved | *1 | Global Humidity`
> packed in bytes 2–3, then a Reserved word at bytes 4–7 with humidity
> placed inside it. The exact bit-packing follows the LSB-first rule:
> `Atmospheric Model Enable` is bit 0 of one byte, with the rest of that
> word reserved. Verify against Figure 54 (PDF page 97) when implementing.

### 0x0B — Environmental Region Control

- **Direction**: Host → IG
- **Opcode**: 11 / 0x0B
- **Total size**: 48 bytes
- **ICD section**: §4.1.11 (PDF page 100)
- **Mandatory each frame**: no

Defines a rounded-rectangle area on the geoid in which atmospheric and
surface conditions can differ from the global values. Up to 256 weather
layers may be hosted within a region (via Weather Control §4.1.12).
Multiple regions can overlap; per-attribute merge flags decide what
happens in the overlap.

**Fields**

| Offset | Size | Name                                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|---------------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                             | unsigned int8   | 11                                              | fixed |
| 1      | 1    | Packet Size                           | unsigned int8   | 48                                              | fixed |
| 2      | 2    | Region ID                             | unsigned int16  | host-assigned                                   |       |
| 4      | —    | Region State (\*1)                    | unsigned 2-bit  | 0 Inactive, 1 Active, 2 Destroyed               | byte 4, bits 1..0 |
| 4      | —    | Merge Weather Properties (\*2)        | 1-bit           | 0 Use Last, 1 Merge                             | byte 4, bit 2 |
| 4      | —    | Merge Aerosol Concentrations (\*3)    | 1-bit           | 0 Use Last, 1 Merge                             | byte 4, bit 3 |
| 4      | —    | Merge Maritime Surface Conditions (\*4) | 1-bit         | 0 Use Last, 1 Merge                             | byte 4, bit 4 |
| 4      | —    | Merge Terrestrial Surface Conditions (\*5) | 1-bit      | 0 Use Last, 1 Merge                             | byte 4, bit 5 |
| 4      | —    | Reserved (\*6)                        | 2-bit           | 0                                               | byte 4, bits 7..6 |
| 5      | 3    | Reserved                              | —               | 0                                               |       |
| 8      | 8    | Latitude                              | double float    | -90.0 .. +90.0 deg                              | region centre |
| 16     | 8    | Longitude                             | double float    | -180.0 .. +180.0 deg                            | region centre |
| 24     | 4    | Size X                                | single float    | metres, > 0                                     | length along local +X |
| 28     | 4    | Size Y                                | single float    | metres, > 0                                     | length along local +Y |
| 32     | 4    | Corner Radius                         | single float    | 0..min(½SizeX, ½SizeY) m                        | 0 → rectangle; ½Size{X,Y} → circle |
| 36     | 4    | Rotation                              | single float    | -180.0 .. +180.0 deg from True N (CW about Z)   |       |
| 40     | 4    | Transition Perimeter                  | single float    | metres, ≥ 0                                     | gradient corridor outside region |
| 44     | 4    | Reserved                              | —               | 0                                               | 8-byte alignment |

**Usage notes**
- Region State Inactive disables every weather layer/surface in the
  region without destroying them; Destroyed permanently removes the
  region and all its children.
- Merge flags drive the Table 17 combining rules (averages, max/min,
  vector sums) for overlapping regions.
- Adjacent regions with `Corner Radius = 0` and matching transition
  perimeters approximate a continuous gridded weather system
  (Figure 61 in the ICD).

### 0x0C — Weather Control

- **Direction**: Host → IG
- **Opcode**: 12 / 0x0C
- **Total size**: 56 bytes
- **ICD section**: §4.1.12 (PDF page 112)
- **Mandatory each frame**: no

Defines a weather *layer* (global, regional, or attached to a weather
entity). Up to 256 layers globally + 256 per region. Aerosol type
follows Layer ID (Ground Fog 0, Cloud Layer 1–3, Rain 4, Snow 5,
Sleet 6, Hail 7, Sand 8, Dust 9; 10–255 IG-defined).

**Fields**

| Offset | Size | Name                            | Type            | Range / Values                                                           | Notes |
|-------:|-----:|---------------------------------|-----------------|--------------------------------------------------------------------------|-------|
| 0      | 1    | Packet ID                       | unsigned int8   | 12                                                                       | fixed |
| 1      | 1    | Packet Size                     | unsigned int8   | 56                                                                       | fixed |
| 2      | 2    | Entity ID / Region ID           | unsigned int16  | meaning controlled by Scope                                              | ignored if Scope = Global |
| 4      | 1    | Layer ID                        | unsigned int8   | 0..9 standard (see above), 10..255 IG-defined                            | ignored if Scope = Entity |
| 5      | 1    | Humidity                        | unsigned int8   | 0..100 %                                                                 |       |
| 6      | —    | Weather Enable (\*1)            | 1-bit           | 0 Disable / 1 Enable                                                     | byte 6, bit 0 |
| 6      | —    | Scud Enable (\*2)               | 1-bit           | 0/1                                                                      | byte 6, bit 1 |
| 6      | —    | Random Winds Enable (\*3)       | 1-bit           | 0/1                                                                      | byte 6, bit 2 |
| 6      | —    | Random Lightning Enable (\*4)   | 1-bit           | 0/1                                                                      | byte 6, bit 3 |
| 6      | —    | Cloud Type (\*5)                | unsigned 4-bit  | 0 None, 1 Altocumulus, 2 Altostratus, 3 Cirrocumulus, 4 Cirrostratus, 5 Cirrus, 6 Cumulonimbus, 7 Cumulus, 8 Nimbostratus, 9 Stratocumulus, 10 Stratus, 11..15 Other | byte 6, bits 7..4 |
| 7      | —    | Scope (\*6)                     | unsigned 2-bit  | 0 Global, 1 Regional, 2 Entity                                           | byte 7, bits 1..0 |
| 7      | —    | Severity (\*7)                  | unsigned 3-bit  | 0..5 (least → most severe)                                               | byte 7, bits 4..2 |
| 7      | —    | Reserved                        | 3-bit           | 0                                                                        | byte 7, bits 7..5 |
| 8      | 4    | Air Temperature                 | single float    | °C                                                                       |       |
| 12     | 4    | Visibility Range                | single float    | metres; overrides global Atmosphere Control                              |       |
| 16     | 4    | Scud Frequency                  | single float    | 0..100 %                                                                 |       |
| 20     | 4    | Coverage                        | single float    | 0..100 %                                                                 |       |
| 24     | 4    | Base Elevation                  | single float    | metres MSL; ignored if Scope = Entity                                    |       |
| 28     | 4    | Thickness                       | single float    | metres; ignored if Scope = Entity                                        | top = base + thickness |
| 32     | 4    | Transition Band                 | single float    | metres above and below layer; ignored if Scope = Entity                  |       |
| 36     | 4    | Horizontal Wind Speed           | single float    | m/s                                                                      |       |
| 40     | 4    | Vertical Wind Speed             | single float    | m/s; +ve updraft                                                         |       |
| 44     | 4    | Wind Direction                  | single float    | 0..360 deg from True North (wind FROM)                                   |       |
| 48     | 4    | Barometric Pressure             | single float    | mb / hPa                                                                 |       |
| 52     | 4    | Aerosol Concentration           | single float    | g/m³                                                                     | aerosol type from Layer ID |

**Usage notes**
- Disabling a layer (`Weather Enable` = 0) leaves it defined but
  invisible — useful for IOS-controlled toggling.
- One layer can carry only one aerosol; for multiple aerosols in one
  region, define multiple layers.
- Within a region, layers always combine; between overlapping regions,
  the merge rule comes from Environmental Region Control (§4.1.11).

### 0x0D — Maritime Surface Conditions Control

- **Direction**: Host → IG
- **Opcode**: 13 / 0x0D
- **Total size**: 24 bytes
- **ICD section**: §4.1.13 (PDF page 119)
- **Mandatory each frame**: no

Sets the surface state of a body of water (height, temperature,
clarity). Pairs with Wave Control (§4.1.14) for wave dynamics.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                              | Notes |
|-------:|-----:|-------------------------------|-----------------|---------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 13                                          | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 24                                          | fixed |
| 2      | 2    | Entity ID / Region ID         | unsigned int16  | per Scope                                   | ignored if Scope = Global |
| 4      | —    | Surface Conditions Enable (\*1) | 1-bit         | 0 Disable / 1 Enable                        | byte 4, bit 0; ignored if Scope = Global |
| 4      | —    | Whitecap Enable (\*2)         | 1-bit           | 0/1                                         | byte 4, bit 1 |
| 4      | —    | Scope (\*3)                   | unsigned 2-bit  | 0 Global, 1 Regional, 2 Entity              | byte 4, bits 3..2 |
| 4      | —    | Reserved                      | 4-bit           | 0                                           | byte 4, bits 7..4 |
| 5      | 3    | Reserved                      | —               | 0                                           |       |
| 8      | 4    | Sea Surface Height            | single float    | metres MSL (also tide level in surf zone)   |       |
| 12     | 4    | Surface Water Temperature     | single float    | °C                                          |       |
| 16     | 4    | Surface Clarity               | single float    | 0..100 % (0 = turbid, 100 = pristine)       |       |
| 20     | 4    | Reserved                      | —               | 0                                           | 8-byte alignment |

### 0x0E — Wave Control

- **Direction**: Host → IG
- **Opcode**: 14 / 0x0E
- **Total size**: 32 bytes
- **ICD section**: §4.1.14 (PDF page 122)
- **Mandatory each frame**: no

Defines a wave (or wave component, when superposed) propagating across
a body of water — height, wavelength, period, direction, breaker
behaviour. Multiple waves form a sea state.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                       | Notes |
|-------:|-----:|-------------------------------|-----------------|------------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 14                                                   | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 32                                                   | fixed |
| 2      | 2    | Entity ID / Region ID         | unsigned int16  | per Scope                                            | ignored if Scope = Global |
| 4      | 1    | Wave ID                       | unsigned int8   | host-assigned wave index                             |       |
| 5      | —    | Wave Enable (\*1)             | 1-bit           | 0 Disable / 1 Enable                                 | byte 5, bit 0 |
| 5      | —    | Scope (\*2)                   | unsigned 2-bit  | 0 Global, 1 Regional, 2 Entity                       | byte 5, bits 2..1 |
| 5      | —    | Breaker Type (\*3)            | unsigned 2-bit  | 0 Plunging, 1 Spilling, 2 Surging                    | byte 5, bits 4..3 |
| 5      | —    | Reserved                      | 3-bit           | 0                                                    | byte 5, bits 7..5 |
| 6      | 2    | Reserved                      | —               | 0                                                    |       |
| 8      | 4    | Wave Height                   | single float    | metres (trough-to-crest), ≥ 0                        |       |
| 12     | 4    | Wavelength                    | single float    | metres, > 0                                          |       |
| 16     | 4    | Period                        | single float    | seconds, > 0                                         |       |
| 20     | 4    | Direction                     | single float    | 0..360 deg from True N (propagation direction)       |       |
| 24     | 4    | Phase Offset                  | single float    | -360.0..+360.0 deg                                   | for multi-wave interference |
| 28     | 4    | Leading                       | single float    | -180.0..+180.0 deg                                   | shapes the crest (0 = sinusoid) |

### 0x0F — Terrestrial Surface Conditions Control

- **Direction**: Host → IG
- **Opcode**: 15 / 0x0F
- **Total size**: 8 bytes
- **ICD section**: §4.1.15 (PDF page 126)
- **Mandatory each frame**: no

Sets a contaminant or condition on the terrain surface (wet, icy,
slushy, sandy, …). Multiple conditions on one surface require
multiple packets. Used for runway-condition QTG cases (RVR, friction
coefficients on landing rollout).

**Fields**

| Offset | Size | Name                            | Type            | Range / Values                                              | Notes |
|-------:|-----:|---------------------------------|-----------------|-------------------------------------------------------------|-------|
| 0      | 1    | Packet ID                       | unsigned int8   | 15                                                          | fixed |
| 1      | 1    | Packet Size                     | unsigned int8   | 8                                                           | fixed |
| 2      | 2    | Entity ID / Region ID           | unsigned int16  | per Scope                                                   | ignored if Scope = Global |
| 4      | 2    | Surface Condition ID            | unsigned int16  | 0 = Dry (reset; clears all conditions in scope); >0 IG-defined |    |
| 6      | —    | Surface Condition Enable (\*1)  | 1-bit           | 0 Disable / 1 Enable                                        | byte 6, bit 0 |
| 6      | —    | Scope (\*2)                     | unsigned 2-bit  | 0 Global, 1 Regional, 2 Entity                              | byte 6, bits 2..1 |
| 6      | —    | Severity                        | unsigned 5-bit  | 0..31 (0 = negligible, 31 = unnavigable)                    | byte 6, bits 7..3 |
| 7      | 1    | Coverage                        | unsigned int8   | 0..100 %                                                    |       |

### 0x10 — View Control

- **Direction**: Host → IG
- **Opcode**: 16 / 0x10
- **Total size**: 32 bytes
- **ICD section**: §4.1.16 (PDF page 129)
- **Mandatory each frame**: no

Attaches a view (or view group) to an entity and positions/orients its eyepoint
relative to the entity's reference point. Order of operation: translate along
entity X/Y/Z, then rotate yaw → pitch → roll about the eyepoint.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 16                                              | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 32                                              | fixed |
| 2      | 2    | View ID           | unsigned int16  | individual view; ignored if Group ID ≠ 0        |       |
| 4      | 1    | Group ID          | unsigned int8   | 0 None, 1..255 view group                       | non-zero → packet applies to group, View ID ignored |
| 5      | —    | X Offset Enable (\*1)  | 1-bit       | 0 Disable / 1 Enable                            | byte 5, bit 0 |
| 5      | —    | Y Offset Enable (\*2)  | 1-bit       | 0/1                                             | byte 5, bit 1 |
| 5      | —    | Z Offset Enable (\*3)  | 1-bit       | 0/1                                             | byte 5, bit 2 |
| 5      | —    | Roll Enable (\*4)      | 1-bit       | 0/1                                             | byte 5, bit 3 |
| 5      | —    | Pitch Enable (\*5)     | 1-bit       | 0/1                                             | byte 5, bit 4 |
| 5      | —    | Yaw Enable (\*6)       | 1-bit       | 0/1                                             | byte 5, bit 5 |
| 5      | —    | Reserved (\*7)         | 2-bit       | 0                                               | byte 5, bits 7..6 |
| 6      | 2    | Entity ID         | unsigned int16  | parent entity to which view/group is attached   |       |
| 8      | 4    | X Offset          | single float    | metres along entity +X                          | datum: entity reference point |
| 12     | 4    | Y Offset          | single float    | metres along entity +Y                          |       |
| 16     | 4    | Z Offset          | single float    | metres along entity +Z                          |       |
| 20     | 4    | Roll              | single float    | -180.0 .. +180.0 deg                            | applied after yaw + pitch |
| 24     | 4    | Pitch             | single float    | -90.0 .. +90.0 deg                              | applied after yaw |
| 28     | 4    | Yaw               | single float    | 0.0 .. 360.0 deg                                | rotation about Z |

**Usage notes**
- Per-DoF Enable flags gate which fields the IG honours; cleared values leave
  the view at its current value. Lets the Host stream only the deltas.
- A sensor (§4.1.17) cannot be assigned to a view group — only to a single view.

### 0x11 — Sensor Control

- **Direction**: Host → IG
- **Opcode**: 17 / 0x11
- **Total size**: 24 bytes
- **ICD section**: §4.1.17 (PDF page 134)
- **Mandatory each frame**: no

Controls a sensor (FLIR / Maverick seeker / generic camera) attached to a
view. Drives on/off, polarity (white-hot vs black-hot), gain/level, AC coupling,
noise, track mode, and selects which response packet (Sensor Response or
Sensor Extended Response) the IG returns each frame.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                              | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 17                                                          | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 24                                                          | fixed |
| 2      | 2    | View ID                       | unsigned int16  | view to which sensor is assigned (cannot be a view group)   |       |
| 4      | 1    | Sensor ID                     | unsigned int8   | host-assigned sensor index                                  |       |
| 5      | —    | Sensor On/Off (\*1)           | 1-bit           | 0 Off / 1 On                                                | byte 5, bit 0 |
| 5      | —    | Polarity (\*2)                | 1-bit           | 0 White hot / 1 Black hot                                   | byte 5, bit 1 |
| 5      | —    | Line-by-Line Dropout Enable (\*3) | 1-bit       | 0 Disable / 1 Enable                                        | byte 5, bit 2 |
| 5      | —    | Automatic Gain (\*4)          | 1-bit           | 0 Disable / 1 Enable                                        | byte 5, bit 3 |
| 5      | —    | Track White/Black (\*5)       | 1-bit           | 0 White / 1 Black                                           | byte 5, bit 4 |
| 5      | —    | Track Mode                    | unsigned 3-bit  | 0 Off, 1 Force Correlate, 2 Scene, 3 Target, 4 Ship, 5..7 Defined by IG | byte 5, bits 7..5 |
| 6      | —    | Response Type (\*6)           | unsigned 1-bit  | 0 Normal (gate position) / 1 Extended (gate + target)       | byte 6, bit 0 |
| 6      | —    | Reserved                      | 7-bit           | 0                                                           | byte 6, bits 7..1 |
| 7      | 1    | Reserved                      | —               | 0                                                           |       |
| 8      | 4    | Gain                          | single float    | 0.0 .. 1.0                                                  | contrast |
| 12     | 4    | Level                         | single float    | 0.0 .. 1.0                                                  | brightness |
| 16     | 4    | AC Coupling                   | single float    | µs, ≥ 0.0                                                   | decay constant |
| 20     | 4    | Noise                         | single float    | 0.0 .. 1.0                                                  | detector noise |

**Usage notes**
- IG returns Sensor Response (§4.2.6) or Sensor Extended Response (§4.2.7)
  every frame while `Sensor On/Off` = On AND `Track Mode` ≠ Off. Switch by
  flipping `Response Type`.
- Track Mode = Force Correlate is the canonical Maverick lock; Scene is
  typical FLIR; Target is contrast lock; Ship adjusts to waterline.

### 0x12 — Motion Tracker Control

- **Direction**: Host → IG
- **Opcode**: 18 / 0x12
- **Total size**: 8 bytes
- **ICD section**: §4.1.18 (PDF page 141)
- **Mandatory each frame**: no

Initialises and updates a tracked input device (head tracker, eye tracker, wand,
trackball) connected to the IG. Per-DoF enables gate which axes the IG honours;
boresight resets the tracker centre.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 18                                              | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 8                                               | fixed |
| 2      | 2    | View/View Group ID            | unsigned int16  | view or group to which tracker is attached      | meaning per View/View Group Select |
| 4      | 1    | Tracker ID                    | unsigned int8   | host-assigned tracker index                     |       |
| 5      | —    | Tracker Enable (\*1)          | 1-bit           | 0 Disable / 1 Enable                            | byte 5, bit 0 |
| 5      | —    | Boresight Enable (\*2)        | 1-bit           | 0 Disable / 1 Enable                            | byte 5, bit 1. Hold high to keep recentring; clear to release |
| 5      | —    | X Enable (\*3)                | 1-bit           | 0/1                                             | byte 5, bit 2 |
| 5      | —    | Y Enable (\*4)                | 1-bit           | 0/1                                             | byte 5, bit 3 |
| 5      | —    | Z Enable (\*5)                | 1-bit           | 0/1                                             | byte 5, bit 4 |
| 5      | —    | Roll Enable (\*6)             | 1-bit           | 0/1                                             | byte 5, bit 5 |
| 5      | —    | Pitch Enable (\*7)            | 1-bit           | 0/1                                             | byte 5, bit 6 |
| 5      | —    | Yaw Enable (\*8)              | 1-bit           | 0/1                                             | byte 5, bit 7 |
| 6      | —    | View/View Group Select (\*9)  | 1-bit           | 0 View / 1 View Group                           | byte 6, bit 0 |
| 6      | —    | Reserved                      | 7-bit           | 0                                               | byte 6, bits 7..1 |
| 7      | 1    | Reserved                      | —               | 0                                               |       |

**Usage notes**
- The Host can request the instantaneous tracker pose by sending a Position
  Request (§4.1.27) with `Object Class` = Motion Tracker (4).
- If a head tracker is wired through the *Host* rather than the IG, the Host
  should consume its data and translate it into View Control packets — this
  packet is only for trackers physically attached to the IG.

### 0x13 — Earth Reference Model Definition

- **Direction**: Host → IG
- **Opcode**: 19 / 0x13
- **Total size**: 24 bytes
- **ICD section**: §4.1.19 (PDF page 145)
- **Mandatory each frame**: no

Overrides the default WGS 84 reference ellipsoid with a host-supplied
equatorial radius and flattening. Used for non-Earth simulations or
non-standard geodetic models.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 19                                              | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 24                                              | fixed |
| 2      | 2    | Reserved          | —               | 0                                               |       |
| 4      | —    | Custom ERM Enable (\*1) | 1-bit     | 0 Disable (use WGS 84) / 1 Enable               | byte 4, bit 0. Default 0 |
| 4      | —    | Reserved          | 7-bit           | 0                                               | byte 4, bits 7..1 |
| 5      | 3    | Reserved          | —               | 0                                               | 8-byte alignment |
| 8      | 8    | Equatorial Radius | double float    | metres; default 6 378 137.0                     | semi-major axis |
| 16     | 8    | Flattening        | double float    | metres; default 1/298.257223563                 | f = (a − b) / a; 0.0 → spherical Earth |

**Usage notes**
- IG echoes the active ERM via the `Earth Reference Model` field of SOF
  (0 = WGS 84, 1 = Host-Defined). If the IG cannot honour the custom ERM it
  reverts to WGS 84 silently.

### 0x14 — Trajectory Definition

- **Direction**: Host → IG
- **Opcode**: 20 / 0x14
- **Total size**: 24 bytes
- **ICD section**: §4.1.20 (PDF page 147)
- **Mandatory each frame**: no

Specifies an acceleration vector + retardation + terminal velocity for an
IG-driven entity (tracer round, particulate debris). Combined with Rate Control
(§4.1.8) for ballistic motion under gravity and drag.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 20                                              | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 24                                              | fixed |
| 2      | 2    | Entity ID         | unsigned int16  | target entity                                   |       |
| 4      | 4    | Acceleration X    | single float    | m/s² along NED X                                | datum: ellipsoid-tangential NED |
| 8      | 4    | Acceleration Y    | single float    | m/s² along NED Y                                |       |
| 12     | 4    | Acceleration Z    | single float    | m/s² along NED Z                                | gravity goes here (+9.81 down) |
| 16     | 4    | Retardation Rate  | single float    | m/s², ≥ 0                                       | applied opposite to instantaneous velocity (drag) |
| 20     | 4    | Terminal Velocity | single float    | m/s, ≥ 0                                        | speed clamp |

### 0x15 — View Definition

- **Direction**: Host → IG
- **Opcode**: 21 / 0x15
- **Total size**: 32 bytes
- **ICD section**: §4.1.21 (PDF page 149)
- **Mandatory each frame**: no

Overrides the IG's default view configuration: projection (perspective vs
ortho), frustum half-angles, near/far clipping, mirror mode, pixel replication,
view group assignment.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 21                                              | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 32                                              | fixed |
| 2      | 2    | View ID                       | unsigned int16  | view to apply                                   |       |
| 4      | 1    | Group ID                      | unsigned int8   | 0 None, 1..255 group                            |       |
| 5      | —    | Near Enable (\*1)             | 1-bit           | 0 Disable / 1 Enable                            | byte 5, bit 0 |
| 5      | —    | Far Enable (\*2)              | 1-bit           | 0/1                                             | byte 5, bit 1 |
| 5      | —    | Left Enable (\*3)             | 1-bit           | 0/1                                             | byte 5, bit 2 |
| 5      | —    | Right Enable (\*4)            | 1-bit           | 0/1                                             | byte 5, bit 3 |
| 5      | —    | Top Enable (\*5)              | 1-bit           | 0/1                                             | byte 5, bit 4 |
| 5      | —    | Bottom Enable (\*6)           | 1-bit           | 0/1                                             | byte 5, bit 5 |
| 5      | —    | Mirror Mode (\*7)             | unsigned 2-bit  | 0 None, 1 Horizontal, 2 Vertical, 3 Both        | byte 5, bits 7..6 |
| 6      | —    | Pixel Replication Mode (\*8)  | unsigned 3-bit  | 0 None, 1 1×2, 2 2×1, 3 2×2, 4..7 Defined by IG | byte 6, bits 2..0 |
| 6      | —    | Projection Type (\*9)         | 1-bit           | 0 Perspective / 1 Orthographic Parallel         | byte 6, bit 3 |
| 6      | —    | Reorder (\*10)                | 1-bit           | 0 No Reorder / 1 Bring to Top                   | byte 6, bit 4 |
| 6      | —    | View Type                     | unsigned 3-bit  | 0..7 IG-defined                                 | byte 6, bits 7..5 |
| 7      | 1    | Reserved                      | —               | 0                                               |       |
| 8      | 4    | Near                          | single float    | metres, > 0 to < Far                            | clipping plane |
| 12     | 4    | Far                           | single float    | metres, > Near                                  |       |
| 16     | 4    | Left                          | single float    | deg, > -90.0 to < Right                         | frustum half-angle |
| 20     | 4    | Right                         | single float    | deg, > Left to < 90.0                           |       |
| 24     | 4    | Top                           | single float    | deg, > Bottom to < 90.0                         |       |
| 28     | 4    | Bottom                        | single float    | deg, > -90.0 to < Top                           |       |

**Usage notes**
- If multiple View Definition packets in the same frame target the same view,
  the *last* one in the message wins.
- View Type lets the host switch a channel between e.g. out-the-window and IR.

### 0x16 — Collision Detection Segment Definition

- **Direction**: Host → IG
- **Opcode**: 22 / 0x16
- **Total size**: 40 bytes
- **ICD section**: §4.1.22 (PDF page 154)
- **Mandatory each frame**: no

Defines a line segment in an entity's body frame along which the IG performs
segment-to-polygon collision testing every frame. Hits are reported via
Collision Detection Segment Notification (§4.2.13).

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 22                                              | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 40                                              | fixed |
| 2      | 2    | Entity ID         | unsigned int16  | parent entity                                   |       |
| 4      | 1    | Segment ID        | unsigned int8   | host-assigned segment index                     | reuse → overwrites |
| 5      | —    | Segment Enable (\*1) | 1-bit        | 0 Disable / 1 Enable                            | byte 5, bit 0 |
| 5      | —    | Reserved          | 7-bit           | 0                                               | byte 5, bits 7..1 |
| 6      | 2    | Reserved          | —               | 0                                               |       |
| 8      | 4    | X1                | single float    | metres along entity +X                          | datum: entity reference point |
| 12     | 4    | Y1                | single float    | metres along entity +Y                          |       |
| 16     | 4    | Z1                | single float    | metres along entity +Z                          |       |
| 20     | 4    | X2                | single float    | metres                                          | other endpoint |
| 24     | 4    | Y2                | single float    | metres                                          |       |
| 28     | 4    | Z2                | single float    | metres                                          |       |
| 32     | 4    | Material Mask     | unsigned int32  | bitmask of material code ranges                 | IG-defined assignments |
| 36     | 4    | Reserved          | —               | 0                                               | 8-byte alignment |

**Usage notes**
- Setting the parent Entity Control's `Collision Detection Enable` = 0 disables
  *all* segments on that entity without destroying them.
- Destroying the entity destroys all its segments.
- Discrete-time tests can miss thin polygons; the IG is responsible for sweep
  testing if it cares about that case.

### 0x17 — Collision Detection Volume Definition

- **Direction**: Host → IG
- **Opcode**: 23 / 0x17
- **Total size**: 48 bytes
- **ICD section**: §4.1.23 (PDF page 158)
- **Mandatory each frame**: no

Defines a sphere or cuboid in an entity's body frame for volume-to-volume
collision testing. Hits are reported via Collision Detection Volume
Notification (§4.2.14). Volumes on the same entity are not tested against each
other.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 23                                              | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 48                                              | fixed |
| 2      | 2    | Entity ID         | unsigned int16  | parent entity                                   |       |
| 4      | 1    | Volume ID         | unsigned int8   | host-assigned volume index                      | reuse → overwrites |
| 5      | —    | Volume Enable (\*1) | 1-bit         | 0 Disable / 1 Enable                            | byte 5, bit 0 |
| 5      | —    | Volume Type (\*2) | 1-bit           | 0 Sphere / 1 Cuboid                             | byte 5, bit 1 |
| 5      | —    | Reserved          | 6-bit           | 0                                               | byte 5, bits 7..2 |
| 6      | 2    | Reserved          | —               | 0                                               |       |
| 8      | 4    | X                 | single float    | metres, volume centre along entity +X           | datum: entity reference point |
| 12     | 4    | Y                 | single float    | metres along entity +Y                          |       |
| 16     | 4    | Z                 | single float    | metres along entity +Z                          |       |
| 20     | 4    | Height / Radius   | single float    | metres, > 0                                     | sphere → radius; cuboid → length along Z |
| 24     | 4    | Width             | single float    | metres, > 0                                     | cuboid → along Y; ignored for sphere |
| 28     | 4    | Depth             | single float    | metres, > 0                                     | cuboid → along X; ignored for sphere |
| 32     | 4    | Roll              | single float    | -180.0 .. +180.0 deg                            | cuboid only; ignored for sphere |
| 36     | 4    | Pitch             | single float    | -90.0 .. +90.0 deg                              | cuboid only |
| 40     | 4    | Yaw               | single float    | 0.0 .. 360.0 deg                                | cuboid only |
| 44     | 4    | Reserved          | —               | 0                                               | 8-byte alignment |

**Usage notes**
- An entity with `Collision Detection Enable` = 0 still has its volumes used as
  *destinations* but never as *sources* — useful for impassable obstacles.
- If two entities both have collision detection enabled, every pair of volumes
  is tested twice (once per source) — typically wasted work, so disable on one
  side when modelling shooter→target.

### 0x18 — HAT/HOT Request

- **Direction**: Host → IG
- **Opcode**: 24 / 0x18
- **Total size**: 32 bytes
- **ICD section**: §4.1.24 (PDF page 164)
- **Mandatory each frame**: no — once per query, or with `Update Period > 0` for periodic responses

Requests the Height Above Terrain (HAT), Height Of Terrain (HOT), or both
(Extended) at a test point. Test point can be in geodetic coordinates or as an
offset from an entity's reference point. IG replies with HAT/HOT Response
(§4.2.2) or HAT/HOT Extended Response (§4.2.3).

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 24                                              | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 32                                              | fixed |
| 2      | 2    | HAT/HOT ID                    | unsigned int16  | host-assigned correlation ID                    | echoed in response |
| 4      | —    | Request Type (\*1)            | unsigned 2-bit  | 0 HAT, 1 HOT, 2 Extended                        | byte 4, bits 1..0 |
| 4      | —    | Coordinate System (\*2)       | 1-bit           | 0 Geodetic / 1 Entity                           | byte 4, bit 2 |
| 4      | —    | Reserved                      | 5-bit           | 0                                               | byte 4, bits 7..3 |
| 5      | 1    | Update Period                 | unsigned int8   | 0 = one-shot; n>0 = response every nth frame    |       |
| 6      | 2    | Entity ID                     | unsigned int16  | required when Coordinate System = Entity        | ignored for Geodetic |
| 8      | 8    | Latitude / X Offset           | double float    | deg (-90..+90) or metres                        | per Coordinate System |
| 16     | 8    | Longitude / Y Offset          | double float    | deg (-180..+180) or metres                      |       |
| 24     | 8    | Altitude / Z Offset           | double float    | metres MSL or metres                            | ignored if Request Type = HOT |

**Usage notes**
- The IG sets `Valid` = 0 in the response if the test point is outside the
  loaded database.
- The Host should not reuse a HAT/HOT ID before the IG has had time to respond
  — pending requests sharing an ID can be lost.
- Used by the Ownship to keep wheels on the ground (gear-point HOT every frame)
  and to drive sensor occlusion testing.

### 0x19 — Line of Sight Segment Request

- **Direction**: Host → IG
- **Opcode**: 25 / 0x19
- **Total size**: 64 bytes
- **ICD section**: §4.1.25 (PDF page 167)
- **Mandatory each frame**: no

Requests an LOS test along a finite segment defined by a source and destination
point (each in geodetic or entity-local coordinates). IG replies with Line of
Sight Response (§4.2.4) or Line of Sight Extended Response (§4.2.5). LOS ID
namespace is shared with `LOS Vector Request` (§4.1.26).

**Fields**

| Offset | Size | Name                              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                         | unsigned int8   | 25                                              | fixed |
| 1      | 1    | Packet Size                       | unsigned int8   | 64                                              | fixed |
| 2      | 2    | LOS ID                            | unsigned int16  | host-assigned correlation ID                    | echoed in response |
| 4      | —    | Request Type (\*1)                | 1-bit           | 0 Basic / 1 Extended                            | byte 4, bit 0 |
| 4      | —    | Source Point Coordinate System (\*2) | 1-bit        | 0 Geodetic / 1 Entity                           | byte 4, bit 1 |
| 4      | —    | Destination Point Coordinate System (\*3) | 1-bit   | 0 Geodetic / 1 Entity                           | byte 4, bit 2 |
| 4      | —    | Response Coordinate System (\*4)  | 1-bit           | 0 Geodetic / 1 Entity                           | byte 4, bit 3 |
| 4      | —    | Destination Entity ID Valid (\*5) | 1-bit           | 0 Not Valid / 1 Valid                           | byte 4, bit 4 |
| 4      | —    | Reserved                          | 3-bit           | 0                                               | byte 4, bits 7..5 |
| 5      | 1    | Alpha Threshold                   | unsigned int8   | 0..255 minimum opacity                          | surfaces below this α don't register |
| 6      | 2    | Source Entity ID                  | unsigned int16  | when source coordinate system = Entity          | ignored if both source/dest are Geodetic |
| 8      | 8    | Source Latitude / X Offset        | double float    | deg (-90..+90) or metres                        | per Source CS |
| 16     | 8    | Source Longitude / Y Offset       | double float    | deg (-180..+180) or metres                      |       |
| 24     | 8    | Source Altitude / Z Offset        | double float    | metres MSL or metres                            |       |
| 32     | 8    | Destination Latitude / X Offset   | double float    | deg or metres                                   | per Destination CS |
| 40     | 8    | Destination Longitude / Y Offset  | double float    | deg or metres                                   |       |
| 48     | 8    | Destination Altitude / Z Offset   | double float    | metres MSL or metres                            |       |
| 56     | 4    | Material Mask                     | unsigned int32  | bitmask, IG-defined material code ranges        |       |
| 60     | 1    | Update Period                     | unsigned int8   | 0 = one-shot; n>0 = every nth frame             |       |
| 61     | 1    | Reserved                          | —               | 0                                               |       |
| 62     | 2    | Destination Entity ID             | unsigned int16  | only used when Destination CS = Entity AND Destination Entity ID Valid = 1 |  |

**Usage notes**
- IG generates one response packet *per intersection* along the segment; an
  empty segment yields one response with `Valid` = 0.
- Don't share LOS IDs with `LOS Vector Request` — both use the same response
  packet types and the IG can't tell them apart.

### 0x1A — Line of Sight Vector Request

- **Direction**: Host → IG
- **Opcode**: 26 / 0x1A
- **Total size**: 56 bytes
- **ICD section**: §4.1.26 (PDF page 174)
- **Mandatory each frame**: no

Requests an LOS test along an azimuth/elevation vector from a source point,
constrained to a min..max range. Used for laser range-finding, weight-on-wheels
testing, target ranging. Shares LOS ID namespace with §4.1.25.

**Fields**

| Offset | Size | Name                              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                         | unsigned int8   | 26                                              | fixed |
| 1      | 1    | Packet Size                       | unsigned int8   | 56                                              | fixed |
| 2      | 2    | LOS ID                            | unsigned int16  | host-assigned correlation ID                    | echoed in response |
| 4      | —    | Request Type (\*1)                | 1-bit           | 0 Basic / 1 Extended                            | byte 4, bit 0 |
| 4      | —    | Source Point Coordinate System (\*2) | 1-bit        | 0 Geodetic / 1 Entity                           | byte 4, bit 1 |
| 4      | —    | Response Coordinate System (\*3)  | 1-bit           | 0 Geodetic / 1 Entity                           | byte 4, bit 2 |
| 4      | —    | Reserved                          | 5-bit           | 0                                               | byte 4, bits 7..3 |
| 5      | 1    | Alpha Threshold                   | unsigned int8   | 0..255 minimum opacity                          |       |
| 6      | 2    | Entity ID                         | unsigned int16  | source entity (when Source CS = Entity)         | ignored if Geodetic |
| 8      | 4    | Azimuth                           | single float    | -180.0 .. +180.0 deg                            | datum: True N (Geodetic) or entity +X (Entity) |
| 12     | 4    | Elevation                         | single float    | -90.0 .. +90.0 deg                              | +ve = away from ellipsoid (Geodetic) or toward entity -Z (Entity) |
| 16     | 4    | Minimum Range                     | single float    | metres, ≥ 0                                     | datum: source point |
| 20     | 4    | Maximum Range                     | single float    | metres, > Minimum Range                         |       |
| 24     | 8    | Source Latitude / X Offset        | double float    | deg or metres                                   | per Source CS |
| 32     | 8    | Source Longitude / Y Offset       | double float    | deg or metres                                   |       |
| 40     | 8    | Source Altitude / Z Offset        | double float    | metres MSL or metres                            |       |
| 48     | 4    | Material Mask                     | unsigned int32  | bitmask of material code ranges                 |       |
| 52     | 1    | Update Period                     | unsigned int8   | 0 = one-shot; n>0 = every nth frame             |       |
| 53     | 3    | Reserved                          | —               | 0                                               |       |

**Usage notes**
- One response per intersection along the vector within `[Min, Max]`. No
  intersection → one response with `Valid` = 0.
- Don't reuse a LOS ID before a response has come back, and don't share IDs
  with `LOS Segment Request`.

### 0x1B — Position Request

- **Direction**: Host → IG
- **Opcode**: 27 / 0x1B
- **Total size**: 8 bytes
- **ICD section**: §4.1.27 (PDF page 180)
- **Mandatory each frame**: no

Queries the current position/orientation of an entity, articulated part, view,
view group, or motion tracker. IG replies with Position Response (§4.2.8).

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 27                                              | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 8                                               | fixed |
| 2      | 2    | Object ID                     | unsigned int16  | per Object Class (Entity/View/Group/Tracker ID; for Articulated Part = parent Entity ID) |  |
| 4      | 1    | Articulated Part ID           | unsigned int8   | only used when Object Class = Articulated Part  |       |
| 5      | —    | Update Mode (\*1)             | 1-bit           | 0 One-Shot / 1 Continuous                       | byte 5, bit 0 |
| 5      | —    | Object Class (\*2)            | unsigned 3-bit  | 0 Entity, 1 Articulated Part, 2 View, 3 View Group, 4 Motion Tracker | byte 5, bits 3..1 |
| 5      | —    | Coordinate System (\*3)       | unsigned 2-bit  | 0 Geodetic, 1 Parent Entity, 2 Submodel         | byte 5, bits 5..4 |
| 5      | —    | Reserved (\*4)                | 2-bit           | 0                                               | byte 5, bits 7..6 |
| 6      | 2    | Reserved                      | —               | 0                                               |       |

**Usage notes**
- `Coordinate System` = Parent Entity is invalid for top-level entities (no
  parent). Submodel is only valid when querying an Articulated Part.
- Motion Tracker class always returns data in the tracker's native frame
  regardless of `Coordinate System`.

### 0x1C — Environmental Conditions Request

- **Direction**: Host → IG
- **Opcode**: 28 / 0x1C
- **Total size**: 32 bytes
- **ICD section**: §4.1.28 (PDF page 182)
- **Mandatory each frame**: no

Asks the IG for the active environmental state at a geodetic point. `Request
Type` is a bitmask — combine 1/2/4/8 to get any combination of maritime
surface, terrestrial surface, weather, and aerosol responses in one go.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                              | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 28                                                          | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 32                                                          | fixed |
| 2      | 1    | Request Type (\*1) | unsigned 4-bit | bitmask: 1 Maritime Surface, 2 Terrestrial Surface, 4 Weather, 8 Aerosol Concentrations | byte 2, bits 3..0 |
| 2      | —    | Reserved          | 4-bit           | 0                                                           | byte 2, bits 7..4 |
| 3      | 1    | Request ID        | unsigned int8   | host-assigned correlation ID                                | echoed in response packets |
| 4      | 4    | Reserved          | —               | 0                                                           | 8-byte alignment |
| 8      | 8    | Latitude          | double float    | -90.0 .. +90.0 deg                                          |       |
| 16     | 8    | Longitude         | double float    | -180.0 .. +180.0 deg                                        |       |
| 24     | 8    | Altitude          | double float    | metres MSL                                                  | only used for weather + aerosol queries |

**Usage notes**
- IG returns at most one Maritime Surface (§4.2.11) and one Weather Conditions
  (§4.2.9) packet per request. It returns one Terrestrial Surface (§4.2.12)
  packet per surface condition type at the point, and one Aerosol Concentration
  (§4.2.10) packet per weather layer (per `Layer ID`) the point falls inside.

### 0x1D — Symbol Surface Definition

- **Direction**: Host → IG
- **Opcode**: 29 / 0x1D
- **Total size**: 56 bytes
- **ICD section**: §4.1.29 (PDF page 187)
- **Mandatory each frame**: no

Creates or modifies a symbol surface — a 2D rectangle in the scene on which
text/circle/line symbols are drawn. Surface attaches to either an entity
(non-billboard or billboard) or a view (HUD-style overlay). Surface stacking
within a view is by Surface ID (low → drawn first).

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 29                                              | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 56                                              | fixed |
| 2      | 2    | Surface ID                    | unsigned int16  | unique within IG                                |       |
| 4      | —    | Surface State (\*1)           | 1-bit           | 0 Active / 1 Destroyed                          | byte 4, bit 0 |
| 4      | —    | Attach Type (\*2)             | 1-bit           | 0 Entity / 1 View                               | byte 4, bit 1 |
| 4      | —    | Billboard (\*3)               | 1-bit           | 0 Non-Billboard / 1 Billboard                   | byte 4, bit 2; ignored when Attach Type = View |
| 4      | —    | Perspective Growth Enable (\*4) | 1-bit         | 0 Disabled / 1 Enabled                          | byte 4, bit 3; only used for entity-attached billboards |
| 4      | —    | Reserved                      | 4-bit           | 0                                               | byte 4, bits 7..4 |
| 5      | 1    | Reserved                      | —               | 0                                               |       |
| 6      | 2    | Entity ID / View ID           | unsigned int16  | per Attach Type                                 | packet ignored if target doesn't exist |
| 8      | 4    | X Offset / Left               | single float    | metres (entity) or 0..1 viewport-width (view)   |       |
| 12     | 4    | Y Offset / Right              | single float    | metres (entity) or 0..1 viewport-width (view)   |       |
| 16     | 4    | Z Offset / Top                | single float    | metres (entity) or 0..1 viewport-height (view)  |       |
| 20     | 4    | Yaw / Bottom                  | single float    | 0..360 deg (entity, ignored for billboard) or 0..1 viewport-height (view) |  |
| 24     | 4    | Pitch                         | single float    | -90.0 .. +90.0 deg                              | non-billboard entity-attached only |
| 28     | 4    | Roll                          | single float    | -180.0 .. +180.0 deg                            | non-billboard entity-attached only |
| 32     | 4    | Width                         | single float    | metres or deg arc                               | meaning depends on Attach Type/Billboard/Perspective Growth |
| 36     | 4    | Height                        | single float    | metres or deg arc                               | same |
| 40     | 4    | Min U                         | single float    | < Max U, surface horizontal units               | UV viewable area |
| 44     | 4    | Max U                         | single float    | > Min U                                         |       |
| 48     | 4    | Min V                         | single float    | > Max V (note: V axis convention)               |       |
| 52     | 4    | Max V                         | single float    | < Min V                                         |       |

**Usage notes**
- View-attached surfaces are coincident with the near clipping plane and draw
  on top of all other objects in the view (HUD overlay).
- Destroying the surface destroys all symbols on it. Destroying the parent
  entity destroys all surfaces attached to it.

### 0x1E — Symbol Text Definition

- **Direction**: Host → IG
- **Opcode**: 30 / 0x1E
- **Total size**: 16..248 bytes (12 + text length, padded to multiple of 8)
- **ICD section**: §4.1.30 (PDF page 194)
- **Mandatory each frame**: no

Creates a text symbol with alignment, orientation, font, and size. UTF-8
string follows the header, NULL-terminated and zero-padded to the next 8-byte
boundary.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                              | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 30                                                          | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 16..248 (= 12 + text length, multiple of 8)                 |       |
| 2      | 2    | Symbol ID         | unsigned int16  | unique among all symbols                                    | reuse with different type → existing symbol destroyed first |
| 4      | —    | Alignment         | unsigned 4-bit  | 0 Top Left, 1 Top Center, 2 Top Right, 3 Center Left, 4 Center, 5 Center Right, 6 Bottom Left, 7 Bottom Center, 8 Bottom Right | byte 4, bits 3..0 |
| 4      | —    | Orientation (\*1) | unsigned 2-bit  | 0 Left→Right, 1 Top→Bottom, 2 Right→Left, 3 Bottom→Top      | byte 4, bits 5..4 |
| 4      | —    | Reserved (\*2)    | 2-bit           | 0                                                           | byte 4, bits 7..6 |
| 5      | 1    | Font ID           | unsigned int8   | 0 IG default; 1..16 standard styles (Sans Serif/Serif × Bold/Italic, monospace at 9..16); 17..255 IG-defined |  |
| 6      | 2    | Reserved          | —               | 0                                                           |       |
| 8      | 4    | Font Size         | single float    | symbol surface vertical units                               |       |
| 12     | text_len | Text          | UTF-8 octets    | NULL-terminated; zero-pad to 8-byte boundary                | max 236 octets + NULL |

**Usage notes**
- New symbols are hidden by default — issue a `Symbol Control` (§4.1.34) with
  `Symbol State` = Visible to display.
- Text symbol type cannot be changed; sending a definition packet of a
  different type with the same ID destroys and recreates.

### 0x1F — Symbol Circle Definition

- **Direction**: Host → IG
- **Opcode**: 31 / 0x1F
- **Total size**: 16..232 bytes (16 + 24 × n; 1 ≤ n ≤ 9)
- **ICD section**: §4.1.31 (PDF page 198)
- **Mandatory each frame**: no

Creates a composite symbol of up to 9 circles or arcs. Drawing style applies
globally (all line, or all fill). Each circle entry is 24 bytes appended after
the 16-byte header.

**Fields (header)**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 31                                              | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 16 + 24×n, n ≤ 9 → max 232                      |       |
| 2      | 2    | Symbol ID             | unsigned int16  | unique among all symbols                        |       |
| 4      | —    | Drawing Style (\*1)   | 1-bit           | 0 Line / 1 Fill                                 | byte 4, bit 0 |
| 4      | —    | Reserved              | 7-bit           | 0                                               | byte 4, bits 7..1 |
| 5      | 1    | Reserved              | —               | 0                                               |       |
| 6      | 2    | Stipple Pattern       | unsigned int16  | bitmask; 0xFFFF = solid                         | ignored if Fill |
| 8      | 4    | Line Width            | single float    | scaled symbol surface units                     | ignored if Fill |
| 12     | 4    | Stipple Pattern Length | single float   | scaled symbol surface units                     | one full pattern repeat; ignored if Fill |

**Fields (per circle, repeats n times starting at offset 16)**

| Offset (relative) | Size | Name        | Type         | Range / Values                                  | Notes |
|------------------:|-----:|-------------|--------------|-------------------------------------------------|-------|
| 0                 | 4    | Center U    | single float | scaled symbol surface units                     |       |
| 4                 | 4    | Center V    | single float | scaled symbol surface units                     |       |
| 8                 | 4    | Radius      | single float | ≥ 0.0                                           | line: centerline radius; fill: outer edge |
| 12                | 4    | Inner Radius| single float | ≥ 0.0 to < Radius                               | fill only; ignored for line |
| 16                | 4    | Start Angle | single float | 0..360 deg, CCW from +U axis                    |       |
| 20                | 4    | End Angle   | single float | 0..360 deg, CCW from +U axis                    | If Start > End → arc crosses +U axis |

**Usage notes**
- Full circle: Start Angle = End Angle (recommend 0.0 = 0.0 to avoid FP error).
- Inner Radius = 0.0 with Fill → completely filled disc.
- Symbol type is locked once created; new definition with same Symbol ID but
  different type destroys and recreates.

### 0x20 — Symbol Line Definition

- **Direction**: Host → IG
- **Opcode**: 32 / 0x20
- **Total size**: 16..248 bytes (16 + 8 × n; n ≤ 29)
- **ICD section**: §4.1.32 (PDF page 205)
- **Mandatory each frame**: no

Defines a points/lines/triangles primitive: zero or more vertices interpreted
per `Primitive Type` (Point / Line / Line Strip / Line Loop / Triangle /
Triangle Strip / Triangle Fan).

**Fields (header)**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 32                                              | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 16 + 8×n, n ≤ 29 → max 248                      |       |
| 2      | 2    | Symbol ID         | unsigned int16  | unique among all symbols                        |       |
| 4      | —    | Primitive Type    | 4-bit field     | 0 Point, 1 Line, 2 Line Strip, 3 Line Loop, 4 Triangle, 5 Triangle Strip, 6 Triangle Fan | byte 4, bits 3..0 |
| 4      | —    | Reserved          | 4-bit           | 0                                               | byte 4, bits 7..4 |
| 5      | 1    | Reserved          | —               | 0                                               |       |
| 6      | 2    | Stipple Pattern   | unsigned int16  | bitmask; 0xFFFF = solid                         | ignored for Point/Triangle/Strip/Fan |
| 8      | 4    | Line Width        | single float    | scaled symbol surface units                     | for Point: point diameter; ignored for Triangle types |
| 12     | 4    | Stipple Pattern Length | single float | scaled symbol surface units                    | one full pattern repeat |

**Fields (per vertex, repeats n times starting at offset 16)**

| Offset (relative) | Size | Name     | Type         | Range / Values                                  |
|------------------:|-----:|----------|--------------|-------------------------------------------------|
| 0                 | 4    | Vertex U | single float | scaled symbol surface units                     |
| 4                 | 4    | Vertex V | single float | scaled symbol surface units                     |

**Usage notes**
- Triangle Strip: vertex order should give CCW winding from view eyepoint —
  back-faces may be culled by the IG.
- Triangle: dangling vertices (count not divisible by 3) are silently dropped.
- Line: odd vertex count → last vertex dropped.

### 0x21 — Symbol Clone

- **Direction**: Host → IG
- **Opcode**: 33 / 0x21
- **Total size**: 8 bytes
- **ICD section**: §4.1.33 (PDF page 210)
- **Mandatory each frame**: no

Creates a new symbol as an exact copy of an existing symbol or as an instance
of an IG-defined symbol template. Subsequent operations on the copy do not
affect the source unless via parent/child hierarchy.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 33                                              | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 8                                               | fixed |
| 2      | 2    | Symbol ID         | unsigned int16  | new symbol's identifier                         | reuse with different type → existing destroyed first |
| 4      | —    | Source Type (\*1) | 1-bit           | 0 Symbol / 1 Symbol Template                    | byte 4, bit 0 |
| 4      | —    | Reserved          | 7-bit           | 0                                               | byte 4, bits 7..1 |
| 5      | 1    | Reserved          | —               | 0                                               |       |
| 6      | 2    | Source ID         | unsigned int16  | symbol-to-copy or template ID                   | invalid → packet ignored |

**Usage notes**
- The clone is hidden by default; reveal with Symbol Control.

### 0x22 — Symbol Control

- **Direction**: Host → IG
- **Opcode**: 34 / 0x22
- **Total size**: 40 bytes
- **ICD section**: §4.1.34 (PDF page 212)
- **Mandatory each frame**: no

Sets every dynamic attribute of a symbol in one packet — visibility, parent
attachment, surface, layer, color, flash cycle, position/rotation, scale.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 34                                              | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 40                                              | fixed |
| 2      | 2    | Symbol ID                     | unsigned int16  | target symbol; invalid → packet ignored         |       |
| 4      | —    | Symbol State (\*1)            | unsigned 2-bit  | 0 Hidden, 1 Visible, 2 Destroyed                | byte 4, bits 1..0 |
| 4      | —    | Attach State (\*2)            | 1-bit           | 0 Detach / 1 Attach                             | byte 4, bit 2 |
| 4      | —    | Flash Control (\*3)           | 1-bit           | 0 Continue / 1 Reset                            | byte 4, bit 3 |
| 4      | —    | Inherit Color (\*4)           | 1-bit           | 0 Not Inherited / 1 Inherited                   | byte 4, bit 4 |
| 4      | —    | Reserved (\*5)                | 3-bit           | 0                                               | byte 4, bits 7..5 |
| 5      | 1    | Reserved                      | —               | 0                                               |       |
| 6      | 2    | Parent Symbol ID              | unsigned int16  | only used if Attach State = Attach              |       |
| 8      | 2    | Surface ID                    | unsigned int16  | symbol surface; ignored for child symbols       |       |
| 10     | 1    | Layer                         | unsigned int8   | 0..255; higher → drawn later                    |       |
| 11     | 1    | Flash Duty Cycle Percentage   | unsigned int8   | 0..100 % visible                                | 0 → always invisible; 100 → always visible |
| 12     | 4    | Flash Period                  | single float    | seconds                                         | ignored if duty cycle 0 or 100 |
| 16     | 4    | Position U                    | single float    | scaled symbol surface units                     |       |
| 20     | 4    | Position V                    | single float    | scaled symbol surface units                     |       |
| 24     | 4    | Rotation                      | single float    | 0..360 deg, CCW about local origin              |       |
| 28     | 1    | Red                           | unsigned int8   | 0..255                                          | ignored if Inherit Color |
| 29     | 1    | Green                         | unsigned int8   | 0..255                                          |       |
| 30     | 1    | Blue                          | unsigned int8   | 0..255                                          |       |
| 31     | 1    | Alpha                         | unsigned int8   | 0 transparent .. 255 opaque                     |       |
| 32     | 4    | Scale U                       | single float    | > 0.0                                           |       |
| 36     | 4    | Scale V                       | single float    | > 0.0                                           |       |

**Usage notes**
- Use Short Symbol Control (§4.1.35) to update one or two attributes — saves
  24 bytes per packet.
- Destroying a parent destroys all children. A flashing parent forces children
  to inherit its duty cycle and period.

### 0x23 — Short Symbol Control

- **Direction**: Host → IG
- **Opcode**: 35 / 0x23
- **Total size**: 16 bytes
- **ICD section**: §4.1.35 (PDF page 220)
- **Mandatory each frame**: no

Lower-bandwidth alternative to Symbol Control (§4.1.34) for changing 1
or 2 attributes of a symbol. Each `Attribute Value` field is interpreted
per the matching `Attribute Select` enum. Use this for high-rate
single-attribute updates (needle position, layer toggle, colour).

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 35                                              | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 16                                              | fixed |
| 2      | 2    | Symbol ID             | unsigned int16  | target symbol                                   |       |
| 4      | —    | Symbol State (\*1)    | 2-bit           | 0 Hidden, 1 Visible, 2 Destroyed                | byte 4, bits 1..0 |
| 4      | —    | Attach State (\*2)    | 1-bit           | 0 Detach / 1 Attach                             | byte 4, bit 2 |
| 4      | —    | Flash Control (\*3)   | 1-bit           | 0 Continue / 1 Reset                            | byte 4, bit 3 |
| 4      | —    | Inherit Color (\*4)   | 1-bit           | 0 Not Inherited / 1 Inherited                   | byte 4, bit 4 |
| 4      | —    | Reserved              | 3-bit           | 0                                               | byte 4, bits 7..5 |
| 5      | 1    | Reserved              | —               | 0                                               |       |
| 6      | 1    | Attribute Select 1    | unsigned int8   | 0 None, 1 Surface ID, 2 Parent Symbol ID, 3 Layer, 4 Flash Duty Cycle %, 5 Flash Period, 6 Position U, 7 Position V, 8 Rotation, 9 Color, 10 Scale U, 11 Scale V | full byte; selects the type of `Attribute Value 1` |
| 7      | 1    | Attribute Select 2    | unsigned int8   | same enum as Select 1                           | full byte |
| 8      | 4    | Attribute Value 1     | word            | type per Attribute Select 1: 1..4 → unsigned int32; 5..8, 10, 11 → single float; 9 → 4×uint8 R/G/B/A (MSB→LSB) | byte-swapped as 32-bit |
| 12     | 4    | Attribute Value 2     | word            | type per Attribute Select 2                     |       |

**Cross-reference (verified 2026-04-23)**
The 16-byte size and 8-bit Attribute Select widths above match Boeing's
own CIGI Class Library:

```c
#define CIGI_SHORT_SYMBOL_CONTROL_PACKET_SIZE_V3_3 16
```
(`include/CigiBaseShortSymbolCtrl.h` in the CCL).

The Wireshark CIGI dissector
(`epan/dissectors/packet-cigi.c::cigi3_3_add_short_symbol_control`) reads
the same 16 bytes; its `attribute_select1` / `attribute_select2` calls use
`offset, 1, ...` (1 byte each), and the value enum
`cigi3_3_short_symbol_control_attribute_select_vals` lists 12 values
(0..11), confirming an 8-bit selector.

(Earlier extractions of this file claimed Figure 108 shows
`Packet Size = 32`. That reading was wrong — the figure layout is for a
16-byte packet; only Symbol Control §4.1.34 is 32 bytes.)

**Usage notes**
- For Attribute Select 1 or 2 = Color (9), the four colour bytes are
  packed MSB→LSB as R / G / B / A so that a 32-bit byte-swap on a
  little-endian receiver recovers them in the correct order — see
  Wireshark dissector for the canonical packing.
- Selecting `None` (0) on an Attribute Select makes the corresponding
  Attribute Value field ignored.

---

## IG → Host packets

### 0x65 — Start of Frame

- **Direction**: IG → Host
- **Opcode**: 101 / 0x65
- **Total size**: 24 bytes
- **ICD section**: §4.2.1 (PDF page 224)
- **Mandatory each frame**: **yes** — every IG→Host datagram must start with exactly one Start of Frame

Signals the beginning of a new IG frame. Carries the IG mode, database load
state, byte-swap magic number, frame number, timestamp, and Host frame number
echo for round-trip latency calculation.

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                                       | Notes |
|-------:|-----:|-----------------------|-----------------|----------------------------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 101                                                                  | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 24                                                                   | fixed |
| 2      | 1    | Major Version         | unsigned int8   | 3                                                                    | CIGI 3.x |
| 3      | 1    | Database Number       | int8            | 0 IG controls loading; 1..127 loaded; -1..-127 loading; -128 unavailable | sign-flip handshake with IG Control |
| 4      | 1    | IG Status Code        | unsigned int8   | 0 Normal; 1..255 IG-defined error codes                              | highest-priority error wins |
| 5      | —    | IG Mode (\*1)         | unsigned 2-bit  | 0 Reset/Standby, 1 Operate, 2 Debug, 3 Offline Maintenance           | byte 5, bits 1..0 |
| 5      | —    | Timestamp Valid (\*2) | 1-bit           | 0 Invalid / 1 Valid                                                  | byte 5, bit 2; required Valid in async |
| 5      | —    | Earth Reference Model (\*3) | 1-bit     | 0 WGS 84 / 1 Host-Defined                                            | byte 5, bit 3 |
| 5      | —    | Minor Version         | 4-bit field     | 2 (CIGI 3.3)                                                         | byte 5, bits 7..4 |
| 6      | 2    | Byte Swap Magic Number| unsigned int16  | **0x8000** when no swap needed; receiver checks for 0x80 in low byte | sender always writes 0x8000 |
| 8      | 4    | IG Frame Number       | unsigned int32  | monotonic, +1 each frame                                             | independent of Host Frame Number |
| 12     | 4    | Timestamp             | unsigned int32  | 10 µs ticks since arbitrary reference                                | required in async; rollover ~12h |
| 16     | 4    | Last Host Frame Number| unsigned int32  | echo of last received IG Control `Host Frame Number`                 | latency measurement |
| 20     | 4    | Reserved              | —               | 0                                                                    | 8-byte alignment |

**Usage notes**
- `IG Mode` = Offline Maintenance is *report-only* — Host cannot command it via
  IG Control. Must be set from the IG's UI; Host must wait for IG to drop back
  to Reset/Standby before sending any further mode commands.
- `Database Number` < 0 means the IG is loading; Host should not assume any
  prior entity definitions still exist.

### 0x66 — HAT/HOT Response

- **Direction**: IG → Host
- **Opcode**: 102 / 0x66
- **Total size**: 16 bytes
- **ICD section**: §4.2.2 (PDF page 229)
- **Mandatory each frame**: no — sent in response to `HAT/HOT Request` with Request Type = HAT (0) or HOT (1)

Returns either the Height Above Terrain or Height Of Terrain at a test point.
For HAT + HOT in one packet plus material code and surface normal, see
HAT/HOT Extended Response (§4.2.3).

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 102                                             | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 16                                              | fixed |
| 2      | 2    | HAT/HOT ID            | unsigned int16  | echo of request                                 |       |
| 4      | —    | Valid (\*1)           | 1-bit           | 0 Invalid (point outside DB) / 1 Valid          | byte 4, bit 0 |
| 4      | —    | Response Type (\*2)   | 1-bit           | 0 HAT / 1 HOT                                   | byte 4, bit 1 |
| 4      | —    | Reserved (\*3)        | 2-bit           | 0                                               | byte 4, bits 3..2 |
| 4      | —    | Host Frame Number LSN (\*4) | 4-bit     | least-significant nybble of `Host Frame Number` | byte 4, bits 7..4; ignored if Update Period was 0 |
| 5      | 3    | Reserved              | —               | 0                                               |       |
| 8      | 8    | Height                | double float    | metres                                          | datum: HAT → terrain/sea; HOT → MSL |

### 0x67 — HAT/HOT Extended Response

- **Direction**: IG → Host
- **Opcode**: 103 / 0x67
- **Total size**: 40 bytes
- **ICD section**: §4.2.3 (PDF page 231)
- **Mandatory each frame**: no — response to `HAT/HOT Request` with Request Type = Extended (2)

Carries HAT *and* HOT plus the material code and surface-normal vector at the
intersected terrain.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 103                                             | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 40                                              | fixed |
| 2      | 2    | HAT/HOT ID                    | unsigned int16  | echo of request                                 |       |
| 4      | —    | Valid (\*1)                   | 1-bit           | 0 Invalid / 1 Valid                             | byte 4, bit 0 |
| 4      | —    | Reserved                      | 3-bit           | 0                                               | byte 4, bits 3..1 |
| 4      | —    | Host Frame Number LSN (\*2)   | 4-bit           | LSN of last received Host Frame Number          | byte 4, bits 7..4 |
| 5      | 3    | Reserved                      | —               | 0                                               |       |
| 8      | 8    | HAT                           | double float    | metres above terrain (negative → below)         | datum: terrain/sea |
| 16     | 8    | HOT                           | double float    | metres MSL                                      | datum: MSL |
| 24     | 4    | Material Code                 | unsigned int32  | IG-defined                                      |       |
| 28     | 4    | Normal Vector Azimuth         | single float    | -180.0 .. +180.0 deg from True North            |       |
| 32     | 4    | Normal Vector Elevation       | single float    | -90.0 .. +90.0 deg from geodetic reference plane |      |
| 36     | 4    | Reserved                      | —               | 0                                               | 8-byte alignment |

**Usage notes**
- Codebase-specific: our X-Plane plugin emits a custom 48-byte response with
  surface_type @24 (octet), static_friction @28 and rolling_friction @32 —
  *not* CIGI 3.3 standard. See "Notes for this codebase" §3 at the top of this file.

### 0x68 — Line of Sight Response

- **Direction**: IG → Host
- **Opcode**: 104 / 0x68
- **Total size**: 16 bytes
- **ICD section**: §4.2.4 (PDF page 234)
- **Mandatory each frame**: no — response to `LOS Segment Request` or `LOS Vector Request` with Request Type = Basic (0)

Carries the range from the LOS source point to one intersection. One response
packet *per intersection*; `Response Count` tells the Host how many to expect.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 104                                             | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 16                                              | fixed |
| 2      | 2    | LOS ID                        | unsigned int16  | echo of request                                 |       |
| 4      | —    | Valid (\*1)                   | 1-bit           | 0 Invalid (no intersection / out of range) / 1 Valid | byte 4, bit 0 |
| 4      | —    | Entity ID Valid (\*2)         | 1-bit           | 0 Invalid (intersection is non-entity) / 1 Valid | byte 4, bit 1 |
| 4      | —    | Visible (\*3)                 | 1-bit           | 0 Occluded / 1 Visible                          | byte 4, bit 2; only meaningful for Segment requests |
| 4      | —    | Reserved (\*4)                | 1-bit           | 0                                               | byte 4, bit 3 |
| 4      | —    | Host Frame Number LSN (\*5)   | 4-bit           | LSN of last received Host Frame Number          | byte 4, bits 7..4 |
| 5      | 1    | Response Count                | unsigned int8   | total number of response packets for this request | Visible = 1 → Response Count = 1 |
| 6      | 2    | Entity ID                     | unsigned int16  | intersected entity                              | only valid if Entity ID Valid = 1 |
| 8      | 8    | Range                         | double float    | metres from LOS source to intersection          |       |

### 0x69 — Line of Sight Extended Response

- **Direction**: IG → Host
- **Opcode**: 105 / 0x69
- **Total size**: 56 bytes
- **ICD section**: §4.2.5 (PDF page 237)
- **Mandatory each frame**: no — response to `LOS Segment/Vector Request` with Request Type = Extended (1)

Adds intersection point (geodetic or entity-relative), surface RGBA, material
code, and surface-normal vector to the basic LOS response.

**Fields**

| Offset | Size | Name                              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                         | unsigned int8   | 105                                             | fixed |
| 1      | 1    | Packet Size                       | unsigned int8   | 56                                              | fixed |
| 2      | 2    | LOS ID                            | unsigned int16  | echo of request                                 |       |
| 4      | —    | Valid (\*1)                       | 1-bit           | 0/1                                             | byte 4, bit 0 |
| 4      | —    | Entity ID Valid (\*2)             | 1-bit           | 0/1                                             | byte 4, bit 1 |
| 4      | —    | Range Valid (\*3)                 | 1-bit           | 0/1                                             | byte 4, bit 2; always 0 for Segment requests |
| 4      | —    | Visible (\*4)                     | 1-bit           | 0 Occluded / 1 Visible                          | byte 4, bit 3 |
| 4      | —    | Host Frame Number LSN (\*5)       | 4-bit           | LSN of last received Host Frame Number          | byte 4, bits 7..4 |
| 5      | 1    | Response Count                    | unsigned int8   | total response packets for this request         |       |
| 6      | 2    | Entity ID                         | unsigned int16  | intersected entity (when Entity ID Valid = 1)   |       |
| 8      | 8    | Range                             | double float    | metres from LOS source to intersection          |       |
| 16     | 8    | Latitude / X Offset               | double float    | deg or metres                                   | per Response Coordinate System of request |
| 24     | 8    | Longitude / Y Offset              | double float    | deg or metres                                   |       |
| 32     | 8    | Altitude / Z Offset               | double float    | metres MSL or metres                            |       |
| 40     | 1    | Red                               | unsigned int8   | 0..255                                          | surface colour at intersection |
| 41     | 1    | Green                             | unsigned int8   | 0..255                                          |       |
| 42     | 1    | Blue                              | unsigned int8   | 0..255                                          |       |
| 43     | 1    | Alpha                             | unsigned int8   | 0..255                                          |       |
| 44     | 4    | Material Code                     | unsigned int32  | IG-defined                                      |       |
| 48     | 4    | Normal Vector Azimuth             | single float    | -180.0 .. +180.0 deg from True North            |       |
| 52     | 4    | Normal Vector Elevation           | single float    | -90.0 .. +90.0 deg from geodetic ref plane      |       |

### 0x6A — Sensor Response

- **Direction**: IG → Host
- **Opcode**: 106 / 0x6A
- **Total size**: 24 bytes
- **ICD section**: §4.2.6 (PDF page 244)
- **Mandatory each frame**: conditional — every frame the sensor is on AND track mode ≠ Off, when Sensor Control's Response Type = Normal (0)

Reports the sensor gate symbol size and position on the sensor display. The
gate position is the (azimuth, elevation) angle from the sensor's viewing
vector to the centre of the track point.

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 106                                             | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 24                                              | fixed |
| 2      | 2    | View ID               | unsigned int16  | view representing the sensor display            |       |
| 4      | 1    | Sensor ID             | unsigned int8   | sensor whose gate is reported                   |       |
| 5      | —    | Sensor Status (\*1)   | unsigned 2-bit  | 0 Searching for target, 1 Tracking target, 2 Impending breaklock, 3 Breaklock | byte 5, bits 1..0 |
| 5      | —    | Reserved              | 6-bit           | 0                                               | byte 5, bits 7..2 |
| 6      | 2    | Reserved              | —               | 0                                               |       |
| 8      | 2    | Gate X Size           | unsigned int16  | pixels or raster lines (per display orientation) |      |
| 10     | 2    | Gate Y Size           | unsigned int16  | pixels or raster lines                          |       |
| 12     | 4    | Gate X Position       | single float    | deg (horizontal angle from viewing vector to gate centre) |  |
| 16     | 4    | Gate Y Position       | single float    | deg (vertical angle from viewing vector)        |       |
| 20     | 4    | Host Frame Number     | unsigned int32  | full Host Frame Number at calculation time      | latency calc |

### 0x6B — Sensor Extended Response

- **Direction**: IG → Host
- **Opcode**: 107 / 0x6B
- **Total size**: 48 bytes
- **ICD section**: §4.2.7 (PDF page 247)
- **Mandatory each frame**: conditional — like Sensor Response, but when Response Type = Extended (1)

Carries gate size/position *plus* the geodetic position of the track point and
the entity ID of the target.

**Fields**

| Offset | Size | Name                          | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                     | unsigned int8   | 107                                             | fixed |
| 1      | 1    | Packet Size                   | unsigned int8   | 48                                              | fixed |
| 2      | 2    | View ID                       | unsigned int16  | view representing the sensor display            |       |
| 4      | 1    | Sensor ID                     | unsigned int8   |                                                 |       |
| 5      | —    | Sensor Status (\*1)           | unsigned 2-bit  | 0 Searching, 1 Tracking, 2 Impending Breaklock, 3 Breaklock | byte 5, bits 1..0 |
| 5      | —    | Entity ID Valid (\*2)         | 1-bit           | 0 Invalid (non-entity target) / 1 Valid         | byte 5, bit 2 |
| 5      | —    | Reserved                      | 5-bit           | 0                                               | byte 5, bits 7..3 |
| 6      | 2    | Entity ID                     | unsigned int16  | target entity (when Entity ID Valid = 1)        |       |
| 8      | 2    | Gate X Size                   | unsigned int16  | pixels / raster lines                           |       |
| 10     | 2    | Gate Y Size                   | unsigned int16  | pixels / raster lines                           |       |
| 12     | 4    | Gate X Offset                 | single float    | deg from viewing vector                         | called Position in §4.2.6 |
| 16     | 4    | Gate Y Offset                 | single float    | deg from viewing vector                         |       |
| 20     | 4    | Host Frame Number             | unsigned int32  | full Host Frame Number at calculation time      |       |
| 24     | 8    | Track Point Latitude          | double float    | -90.0 .. +90.0 deg                              | only valid when Sensor Status = 1 or 2 |
| 32     | 8    | Track Point Longitude         | double float    | -180.0 .. +180.0 deg                            |       |
| 40     | 8    | Track Point Altitude          | double float    | metres MSL                                      |       |

### 0x6C — Position Response

- **Direction**: IG → Host
- **Opcode**: 108 / 0x6C
- **Total size**: 48 bytes
- **ICD section**: §4.2.8 (PDF page 250)
- **Mandatory each frame**: no — response to `Position Request` (§4.1.27)

Reports current pose of an entity, articulated part, view, view group, or
motion tracker.

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 108                                             | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 48                                              | fixed |
| 2      | 2    | Object ID             | unsigned int16  | target object (per Object Class)                |       |
| 4      | 1    | Articulated Part ID   | unsigned int8   | only valid when Object Class = Articulated Part |       |
| 5      | —    | Object Class (\*1)    | unsigned 3-bit  | 0 Entity, 1 Articulated Part, 2 View, 3 View Group, 4 Motion Tracker | byte 5, bits 2..0 |
| 5      | —    | Coordinate System (\*2) | unsigned 2-bit | 0 Geodetic, 1 Parent Entity, 2 Submodel        | byte 5, bits 4..3 |
| 5      | —    | Reserved              | 3-bit           | 0                                               | byte 5, bits 7..5 |
| 6      | 2    | Reserved              | —               | 0                                               |       |
| 8      | 8    | Latitude / X Offset   | double float    | deg or metres                                   | per Coordinate System |
| 16     | 8    | Longitude / Y Offset  | double float    | deg or metres                                   |       |
| 24     | 8    | Altitude / Z Offset   | double float    | metres MSL or metres                            |       |
| 32     | 4    | Roll                  | single float    | -180.0 .. +180.0 deg                            |       |
| 36     | 4    | Pitch                 | single float    | -90.0 .. +90.0 deg                              |       |
| 40     | 4    | Yaw                   | single float    | 0.0 .. 360.0 deg                                |       |
| 44     | 4    | Reserved              | —               | 0                                               | 8-byte alignment |

**Usage notes**
- For Motion Tracker (Object Class = 4), Coordinate System is ignored — position
  is always reported in the tracker's own boresight frame.
- Parent Entity coordinate system is invalid for top-level entities.

### 0x6D — Weather Conditions Response

- **Direction**: IG → Host
- **Opcode**: 109 / 0x6D
- **Total size**: 32 bytes
- **ICD section**: §4.2.9 (PDF page 255)
- **Mandatory each frame**: no — response to `Environmental Conditions Request` with Request Type bit 4 (Weather Conditions = 4) set

Reports the weather state (humidity, temperature, visibility, wind, pressure)
at the requested geodetic point.

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 109                                             | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 32                                              | fixed |
| 2      | 1    | Request ID            | unsigned int8   | echo of request                                 |       |
| 3      | 1    | Humidity              | unsigned int8   | 0..100 %                                        |       |
| 4      | 4    | Air Temperature       | single float    | °C                                              |       |
| 8      | 4    | Visibility Range      | single float    | metres, ≥ 0                                     |       |
| 12     | 4    | Horizontal Wind Speed | single float    | m/s, ≥ 0                                        | datum: ellipsoid-tangential ref plane |
| 16     | 4    | Vertical Wind Speed   | single float    | m/s; +ve = updraft                              |       |
| 20     | 4    | Wind Direction        | single float    | 0..360 deg from True North (wind FROM)          |       |
| 24     | 4    | Barometric Pressure   | single float    | mb / hPa, ≥ 0                                   |       |
| 28     | 4    | Reserved              | —               | 0                                               |       |

### 0x6E — Aerosol Concentration Response

- **Direction**: IG → Host
- **Opcode**: 110 / 0x6E
- **Total size**: 8 bytes
- **ICD section**: §4.2.10 (PDF page 258)
- **Mandatory each frame**: no — response to `Environmental Conditions Request` with Request Type bit 8 (Aerosol = 8) set; one packet per weather layer Layer ID at the test point

Reports the airborne particle concentration at the requested point. Aerosol
type is implicit in `Layer ID` (Ground Fog 0, Cloud 1..3, Rain 4, Snow 5,
Sleet 6, Hail 7, Sand 8, Dust 9, IG-defined 10..255).

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 110                                             | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 8                                               | fixed |
| 2      | 1    | Request ID            | unsigned int8   | echo of request                                 |       |
| 3      | 1    | Layer ID              | unsigned int8   | weather-layer ID (also implies aerosol type)    |       |
| 4      | 4    | Aerosol Concentration | single float    | g/m³, ≥ 0                                       |       |

**Usage notes**
- Two overlapping layers with the same Layer ID → one response with the
  *averaged* concentration. Two overlapping layers with different Layer IDs
  → two response packets.

### 0x6F — Maritime Surface Conditions Response

- **Direction**: IG → Host
- **Opcode**: 111 / 0x6F
- **Total size**: 16 bytes
- **ICD section**: §4.2.11 (PDF page 260)
- **Mandatory each frame**: no — response to `Environmental Conditions Request` with Request Type bit 1 (Maritime Surface = 1) set

Reports sea surface state at the requested geodetic point — equilibrium height
(no waves), water temperature, surface clarity. For instantaneous wave-driven
elevation, use HOT request instead.

**Fields**

| Offset | Size | Name                      | Type            | Range / Values                                  | Notes |
|-------:|-----:|---------------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID                 | unsigned int8   | 111                                             | fixed |
| 1      | 1    | Packet Size               | unsigned int8   | 16                                              | fixed |
| 2      | 1    | Request ID                | unsigned int8   | echo of request                                 |       |
| 3      | 1    | Reserved                  | —               | 0                                               |       |
| 4      | 4    | Sea Surface Height        | single float    | metres MSL                                      | equilibrium (no wave displacement) |
| 8      | 4    | Surface Water Temperature | single float    | °C                                              |       |
| 12     | 4    | Surface Clarity           | single float    | 0..100 % (0 turbid, 100 pristine)               |       |

### 0x70 — Terrestrial Surface Conditions Response

- **Direction**: IG → Host
- **Opcode**: 112 / 0x70
- **Total size**: 8 bytes
- **ICD section**: §4.2.12 (PDF page 262)
- **Mandatory each frame**: no — response to `Environmental Conditions Request` with Request Type bit 2 (Terrestrial Surface = 2) set; one packet per surface condition active at the point

Reports a surface condition / contaminant at the requested geodetic point. IG
sends one packet per active condition.

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 112                                             | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 8                                               | fixed |
| 2      | 1    | Request ID            | unsigned int8   | echo of request                                 |       |
| 3      | 1    | Reserved              | —               | 0                                               |       |
| 4      | 4    | Surface Condition ID  | unsigned int32  | 0..65535 (IG-defined; 0 = Dry)                  |       |

### 0x71 — Collision Detection Segment Notification

- **Direction**: IG → Host
- **Opcode**: 113 / 0x71
- **Total size**: 16 bytes
- **ICD section**: §4.2.13 (PDF page 264)
- **Mandatory each frame**: no — sent each frame a defined collision detection segment (§4.1.22) intersects a polygon

Reports a segment-vs-polygon collision: which segment, which entity owns it,
which entity (if any) was hit, the surface material code, and the distance
along the segment to the intersection.

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 113                                             | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 16                                              | fixed |
| 2      | 2    | Entity ID             | unsigned int16  | entity owning the segment                       |       |
| 4      | 1    | Segment ID            | unsigned int8   | segment-within-entity                           |       |
| 5      | —    | Collision Type (\*1)  | 1-bit           | 0 Non-entity (terrain etc.) / 1 Entity          | byte 5, bit 0 |
| 5      | —    | Reserved              | 7-bit           | 0                                               | byte 5, bits 7..1 |
| 6      | 2    | Contacted Entity ID   | unsigned int16  | other entity hit                                | ignored if Collision Type = Non-entity |
| 8      | 4    | Material Code         | unsigned int32  | IG-defined                                      |       |
| 12     | 4    | Intersection Distance | single float    | metres along segment from (X1,Y1,Z1) endpoint   |       |

**Usage notes**
- If the segment passes through multiple polygons the IG returns only the
  closest intersection.
- Polygons belonging to the same entity as the segment are not tested.

### 0x72 — Collision Detection Volume Notification

- **Direction**: IG → Host
- **Opcode**: 114 / 0x72
- **Total size**: 16 bytes
- **ICD section**: §4.2.14 (PDF page 266)
- **Mandatory each frame**: no — sent each frame two collision detection volumes (§4.1.23) intersect

Reports a volume-vs-volume collision. One packet *per volume involved* — two
volumes colliding generates two notifications (one from each side's
perspective). Volumes on the same entity are not tested against each other.

**Fields**

| Offset | Size | Name                  | Type            | Range / Values                                  | Notes |
|-------:|-----:|-----------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID             | unsigned int8   | 114                                             | fixed |
| 1      | 1    | Packet Size           | unsigned int8   | 16                                              | fixed |
| 2      | 2    | Entity ID             | unsigned int16  | entity owning the source volume                 |       |
| 4      | 1    | Volume ID             | unsigned int8   | source volume index                             |       |
| 5      | —    | Collision Type (\*1)  | 1-bit           | 0 Non-entity / 1 Entity                         | byte 5, bit 0 |
| 5      | —    | Reserved              | 7-bit           | 0                                               | byte 5, bits 7..1 |
| 6      | 2    | Contacted Entity ID   | unsigned int16  | hit entity                                      | ignored if Collision Type = Non-entity |
| 8      | 1    | Contacted Volume ID   | unsigned int8   | hit volume index on contacted entity            |       |
| 9      | 7    | Reserved              | —               | 0                                               | 8-byte alignment |

**Usage notes**
- No spatial intersection data — volume-volume hits typically cover an
  irregular region, so only volume IDs and entity IDs are returned.
- No material code (volumes don't have polygon surfaces).

### 0x73 — Animation Stop Notification

- **Direction**: IG → Host
- **Opcode**: 115 / 0x73
- **Total size**: 8 bytes
- **ICD section**: §4.2.15 (PDF page 268)
- **Mandatory each frame**: no — sent once when an entity's animation reaches the end of its sequence

Tells the Host that a non-continuous animation has finished. Continuous
animations never emit this packet.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 115                                             | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 8                                               | fixed |
| 2      | 2    | Entity ID         | unsigned int16  | entity whose animation stopped                  |       |
| 4      | 4    | Reserved          | —               | 0                                               |       |

### 0x74 — Event Notification

- **Direction**: IG → Host
- **Opcode**: 116 / 0x74
- **Total size**: 16 bytes
- **ICD section**: §4.2.16 (PDF page 269)
- **Mandatory each frame**: no — sent when an IG-defined event fires

Carries an Event ID plus three 32-bit user-defined data words. Host enables/
disables individual events through Component Control with Component Class =
Event (12).

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 116                                             | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 16                                              | fixed |
| 2      | 2    | Event ID          | unsigned int16  | IG-defined                                      |       |
| 4      | 4    | Event Data 1      | word            | event-specific 32-bit datum                     | byte-swapped as 32-bit |
| 8      | 4    | Event Data 2      | word            | event-specific                                  |       |
| 12     | 4    | Event Data 3      | word            | event-specific                                  |       |

### 0x75 — Image Generator Message

- **Direction**: IG → Host
- **Opcode**: 117 / 0x75
- **Total size**: 8..104 bytes (4 + message length, padded to multiple of 8)
- **ICD section**: §4.2.17 (PDF page 271)
- **Mandatory each frame**: no — recommended only in Debug mode

Carries an error/debug/log text message from the IG to the Host. UTF-8 string,
NULL-terminated, zero-padded to the next 8-byte boundary.

**Fields**

| Offset | Size | Name              | Type            | Range / Values                                  | Notes |
|-------:|-----:|-------------------|-----------------|-------------------------------------------------|-------|
| 0      | 1    | Packet ID         | unsigned int8   | 117                                             | fixed |
| 1      | 1    | Packet Size       | unsigned int8   | 8..104 (= 4 + message length, multiple of 8)    |       |
| 2      | 2    | Message ID        | unsigned int16  | IG-defined message identifier                   |       |
| 4      | msg_len | Message        | UTF-8 octets    | NULL-terminated; zero-pad to 8-byte boundary    | max 100 octets including NULL |

**Usage notes**
- Real-time IGs should restrict use to Debug mode (file/console I/O is rarely
  hard real-time).

---

## CIGI 3.x change history

Extracted verbatim from Appendix B of the ICD (doc pages 264–266 = PDF pages
276–278). Use this to know which fields a given CIGI 3.x version supports.

### Version 3.0

Numerous changes from CIGI 2 (no further detail in the ICD).

### Version 3.1

No data format changes from CIGI 3.0.

### Version 3.2

**General changes**
- Added a Minor Version number to the interface. Minor version changes can
  now contain data format changes.
- Modified the use of the Frame Counter mechanism.
- Added capabilities for continuous, periodic HAT/HOT and LOS responses from
  a single request.

**IG Control (0x01)**
- Renamed `CIGI Version` → `Major Version`.
- Added `Minor Version`.
- Renamed `Byte Swap` → `Byte Swap Magic Number`.
- Modified the use of the Frame Counter parameter and renamed it to
  `Host Frame Number`.
- Added `Last IG Frame Number`.

**Rate Control (0x08)**
- Added `Coordinate System` parameter and modified reference data for linear
  and angular rates.

**HAT/HOT Request (0x18)**
- Added `Update Period`.

**LOS Segment Request (0x19)**
- Added `Update Period`.
- Added `Destination Entity ID` and `Destination Entity ID Valid`.

**LOS Vector Request (0x1A)**
- Added `Update Period`.

**Start of Frame (0x65)**
- Renamed `CIGI Version` → `Major Version`; added `Minor Version`.
- Renamed `Byte Swap` → `Byte Swap Magic Number`.
- Modified the use of the Frame Counter parameter and renamed it to
  `IG Frame Number`.
- Added `Last Host Frame Number`.

**HAT/HOT Response (0x66) / HAT/HOT Extended Response (0x67) /
LOS Response (0x68)**
- Added `Host Frame Number LSN`.

**LOS Extended Response (0x69)**
- Removed `Intersection Point Coordinate System`.
- Added `Host Frame Number LSN`.

**Sensor Response (0x6A) / Sensor Extended Response (0x6B)**
- Renamed `Frame Counter` → `Host Frame Number` and changed its usage to
  reflect the Host frame number.

### Version 3.3

**General changes**
- Added support for 2D symbology.
- Changed text representation from ANSI to UTF-8.

**IG Control (0x01)**
- Added `Extrapolation/Interpolation Enable` flag (global entity smoothing).

**Entity Control (0x02)**
- Added `Linear Extrapolation/Interpolation Enable` flag (per-entity smoothing).

**Component Control (0x04) / Short Component Control (0x05)**
- Added `Symbol Surface` and `Symbol` component classes.

**Celestial Sphere Control (0x09)**
- Changed the time specified by `Hour` and `Minute` from local time to UTC.

**New packets in 3.3**
- `Symbol Surface Definition` (0x1D)
- `Symbol Text Definition` (0x1E)
- `Symbol Circle Definition` (0x1F)
- `Symbol Line Definition` (0x20)
- `Symbol Clone` (0x21)
- `Symbol Control` (0x22)
- `Short Symbol Control` (0x23)

## Errata applied

Sourced from Appendix C of `CIGI_ICD_3_3.pdf` (doc page 267 = PDF page 279)
and the standalone `CIGI_3_3_Appendix_C.pdf` errata supplement. Each
erratum below is also annotated as `**Errata applied**: ...` under the
relevant packet entry where applicable.

**Status as shipped:** the in-document Appendix C table (page 267) is
**empty** — the originally-published ICD records no in-band corrections.
The standalone `CIGI_3_3_Appendix_C.pdf` is also empty: no superseding
sheets have been issued by the CIGI working group as of the version of
the appendix in this repository.

If the SISO CIGI working group publishes errata for 3.3 in the future,
add them here in the form:

```
- YYYY-MM-DD — §4.x.N (page Y) — <description of change>.
```

…and add a matching `**Errata applied**: ...` line under the affected
packet entry above.
