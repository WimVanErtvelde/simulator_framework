---
name: cigi-spec
description: >-
  Reference for the Common Image Generator Interface (CIGI) — the packet-based protocol
  between a host simulator and an Image Generator (IG). Covers versions 3.3 and 4.0.
  Use when working on IG integration in simulator_framework: designing or modifying the
  host↔IG adapter, implementing Entity Control, View Control, Component Control,
  Environmental/Atmosphere/Celestial Control, HAT/HOT, Line-of-Sight, or Collision
  Detection packets, debugging CIGI traffic, or choosing between CIGI versions for a
  new IG integration. Also trigger on: CIGI, IG, image generator, host-IG, visual
  system protocol, CGL (CIGI Class Library), Start of Frame, IG Control, packet ID,
  sync group, or any request to look up or implement a specific CIGI packet layout.
---

# CIGI Protocol Reference

SISO-standard protocol for host ↔ Image Generator communication in flight simulation.
This skill holds the packet catalogs and integration patterns needed to implement the
host side of the protocol in `simulator_framework`.

## When to consult

- Implementing or modifying the host↔IG adapter (typically the `sim_visual` package)
- Producing or consuming CIGI packets in any language (C++ in ROS2, Python for tooling)
- Debugging captured CIGI traffic (Wireshark, hex dumps)
- Choosing between CIGI 3.3 (widely deployed) and 4.0 (current) for a new IG
- Mapping ROS2 topic data ↔ CIGI packets

## Protocol essentials (memorise these)

- **Transport**: UDP, separate unicast streams for Host→IG and IG→Host (typically on
  different ports).
- **Byte order**: network byte order (big-endian) in CIGI 3.x. CIGI 4.0 clarifies
  byte-order handling — verify against the v4.0 ICD.
- **Framing**: every Host→IG datagram starts with an **IG Control** packet; every
  IG→Host datagram starts with a **Start of Frame** packet. Subsequent packets
  concatenate within the same UDP payload up to MTU.
- **Frame sync**: IG Control carries the host frame counter. The IG echoes the last
  processed host frame counter back in Start of Frame. This is how latency and
  dropped frames are measured.
- **Packet ID width**: 1 byte in CIGI 3.x, widened in CIGI 4.0. Always verify the
  version before reading captures byte-by-byte.
- **No retransmission**: CIGI is fire-and-forget over UDP. Hosts must be robust to
  packet loss and send full-state packets (not deltas) for any entity that must be
  visible every frame.

## Reference files

Load only what the current task needs.

- `references/cigi-overview.md` — protocol architecture, framing, sync, endianness,
  version landscape, sync group semantics, CGL library notes.
- `references/cigi-4.0-packets.md` — CIGI 4.0 packet catalog with field offsets and
  value ranges. **Populated from the CIGI 4.0 ICD.**
- `references/cigi-3.3-packets.md` — CIGI 3.3 packet catalog (for legacy IG support
  or integration with older IGs). **Populated from the CIGI 3.3 ICD.**
- `references/integration-patterns.md` — patterns for wiring CIGI into simulator_framework
  (ROS2 topic ↔ packet mappings, adapter layout, failure integration).

## Conventions used in the packet catalog files

Every packet entry is structured the same way:

- **Packet ID** (hex) and **name**
- **Direction** (Host→IG or IG→Host)
- **Total size** in bytes
- **Field table**: offset, size, name, type, valid range, notes
- **Usage notes**: typical frequency, dependencies on other packets, version quirks

This keeps the catalog files greppable and copy-pasteable into code comments.

## Related skills

- `sim-framework` — `simulator_framework` architecture; the CIGI adapter package slots
  into the standalone-library + ROS2-node pattern described there.
- `cs-fstd-spec` — qualification requirements that constrain visual-system behaviour
  (field-of-view, latency, cross-cockpit visuals).
