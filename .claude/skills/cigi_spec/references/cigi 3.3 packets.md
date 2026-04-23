# CIGI 3.3 Packet Catalog

Detailed packet layouts for CIGI 3.3 — the most widely deployed CIGI version
across commercial and military IGs. Populated from the official CIGI 3.3 ICD.

> **Populating this file**: Copy the field tables verbatim from the ICD. Do not
> paraphrase offsets or sizes. Use the same template as `cigi-4.0-packets.md`.

Key difference from 4.0: **Packet ID is 1 byte** (range 0x00–0xFF).

## Template (copy per packet)

```
### 0xNN — Packet Name

- **Direction**: Host→IG | IG→Host
- **Total size**: NN bytes
- **Required every frame**: yes | no | conditional

**Fields**

| Offset | Size | Name              | Type    | Range / Values        | Notes |
|--------|------|-------------------|---------|------------------------|-------|
| 0      | 1    | Packet ID         | uint8   | 0xNN                   | fixed |
| 1      | 1    | Packet Size       | uint8   | NN                     | fixed |
| ...    | ...  | ...               | ...     | ...                    | ...   |

**Usage notes**
- typical send rate
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

## Extraction checklist

Same as 4.0 — offsets verbatim, bitfields expanded, padding tracked, deprecated
fields marked.

## 3.3 → 4.0 migration notes

Keep any notable differences uncovered during extraction here rather than in the 4.0
file, so the "legacy" document owns its own historical context:

- *TODO: note fields/packets that changed semantics between 3.3 and 4.0*
- *TODO: note any deprecated-in-4.0 packets that 3.3 IGs still require*
