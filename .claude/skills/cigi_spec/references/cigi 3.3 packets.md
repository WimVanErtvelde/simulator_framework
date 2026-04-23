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
*TODO: extract from CIGI 3.3 ICD*

### 0x02 — Entity Control
*TODO*

### 0x03 — Conformal Clamped Entity Control
*TODO*

### 0x04 — Component Control
*TODO*

### 0x05 — Short Component Control
*TODO*

### 0x06 — Articulated Part Control
*TODO*

### 0x07 — Short Articulated Part Control
*TODO*

### 0x08 — Rate Control
*TODO*

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
