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
*TODO*

### 0x0A — Atmosphere Control
*TODO*

### 0x0B — Environmental Region Control
*TODO*

### 0x0C — Weather Control
*TODO*

### 0x0D — Maritime Surface Conditions Control
*TODO*

### 0x0E — Wave Control
*TODO*

### 0x0F — Terrestrial Surface Conditions Control
*TODO*

### 0x10 — View Control
*TODO*

### 0x11 — Sensor Control
*TODO*

### 0x12 — Motion Tracker Control
*TODO*

### 0x13 — Earth Reference Model Definition
*TODO*

### 0x14 — Trajectory Definition
*TODO*

### 0x15 — View Definition
*TODO*

### 0x16 — Collision Detection Segment Definition
*TODO*

### 0x17 — Collision Detection Volume Definition
*TODO*

### 0x18 — HAT/HOT Request
*TODO*

### 0x19 — Line of Sight Segment Request
*TODO*

### 0x1A — Line of Sight Vector Request
*TODO*

### 0x1B — Position Request
*TODO*

### 0x1C — Environmental Conditions Request
*TODO*

### 0x1D — Symbol Surface Definition
*TODO*

### 0x1E — Symbol Text Definition
*TODO*

### 0x1F — Symbol Circle Definition
*TODO*

### 0x20 — Symbol Line Definition
*TODO*

### 0x21 — Symbol Clone
*TODO*

### 0x22 — Symbol Control
*TODO*

### 0x23 — Short Symbol Control
*TODO*

---

## IG → Host packets

### 0x65 — Start of Frame
*TODO*

### 0x66 — HAT/HOT Response
*TODO*

### 0x67 — HAT/HOT Extended Response
*TODO*

### 0x68 — Line of Sight Response
*TODO*

### 0x69 — Line of Sight Extended Response
*TODO*

### 0x6A — Sensor Response
*TODO*

### 0x6B — Sensor Extended Response
*TODO*

### 0x6C — Position Response
*TODO*

### 0x6D — Weather Conditions Response
*TODO*

### 0x6E — Aerosol Concentration Response
*TODO*

### 0x6F — Maritime Surface Conditions Response
*TODO*

### 0x70 — Terrestrial Surface Conditions Response
*TODO*

### 0x71 — Collision Detection Segment Notification
*TODO*

### 0x72 — Collision Detection Volume Notification
*TODO*

### 0x73 — Animation Stop Notification
*TODO*

### 0x74 — Event Notification
*TODO*

### 0x75 — Image Generator Message
*TODO*

---

## CIGI 3.x change history

*Populated from Appendix B of the ICD (PDF pages 276–278).*

*TODO*

## Errata applied

*Populated from Appendix C (PDF page 279) and the standalone Appendix C
errata PDF (`source/CIGI_3_3_Appendix_C.pdf`). Each erratum is also
noted under the affected packet entry.*

*TODO*
