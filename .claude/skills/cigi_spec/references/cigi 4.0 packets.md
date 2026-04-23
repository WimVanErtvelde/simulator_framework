# CIGI 4.0 Packet Catalog

Detailed packet layouts for CIGI 4.0. Populated from the official CIGI 4.0 ICD.

> **Populating this file**: Copy the field tables verbatim from the ICD. Do not
> paraphrase offsets or sizes. When in doubt, prefer the ICD's wording. Every packet
> entry uses the same template below.

## Template (copy per packet)

```
### 0xNN — Packet Name

- **Direction**: Host→IG | IG→Host
- **Total size**: NN bytes
- **Required every frame**: yes | no | conditional
- **Depends on**: other packet names this packet references or is dominated by

**Fields**

| Offset | Size | Name              | Type    | Range / Values        | Notes |
|--------|------|-------------------|---------|------------------------|-------|
| 0      | 2    | Packet ID         | uint16  | 0xNN                   | fixed |
| 2      | 2    | Packet Size       | uint16  | NN                     | fixed |
| ...    | ...  | ...               | ...     | ...                    | ...   |

**Usage notes**
- typical send rate / triggering conditions
- version quirks vs CIGI 3.3
- common pitfalls
```

---

## Host → IG packets

### 0x01 — IG Control
*TODO: extract from ICD §4.1.1*

### 0x02 — Entity Control
*TODO: extract from ICD §4.1.2*

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

<!-- Add any CIGI 4.0 packets introduced beyond this list -->

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

<!-- CIGI 4.0 packet IDs above are educated guesses based on the 3.3 layout.
     Replace with the actual values from the CIGI 4.0 ICD during extraction.
     CIGI 4.0 uses a wider Packet ID field than 3.x — verify width and range. -->

---

## Extraction checklist

When filling this catalog from the ICD:

- [ ] Replace every packet ID with the actual hex value from the ICD
- [ ] Fill every field table offset-by-offset from the ICD — do not summarise
- [ ] Note any fields that are bitfields or enums (list the valid values inline)
- [ ] Mark any deprecated/reserved fields explicitly
- [ ] Capture padding/reserved bytes — they affect total size
- [ ] Cross-reference any packets that depend on others (e.g. Entity Control depends
      on a prior Component Control to define the entity type)
