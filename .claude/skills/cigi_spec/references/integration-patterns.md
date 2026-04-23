# CIGI Protocol Overview

High-level architecture and concepts that apply across CIGI versions. For
version-specific packet layouts, see `cigi-4.0-packets.md` or `cigi-3.3-packets.md`.

## What CIGI is

CIGI (Common Image Generator Interface) is a SISO-maintained open standard defining
the interface between a **Host** (the flight simulator, running the FDM, systems, and
scenario logic) and one or more **Image Generators** (IGs — the visual rendering
subsystems producing out-the-window imagery, sensor views, HUD symbology, etc.).

The Host is authoritative for world state. The IG is a rendering slave: it receives
entity positions, view parameters, environmental conditions, and control commands,
and reports back on rendering-derived data (HAT/HOT altitudes, line-of-sight
intersections, collision detections, IG status).

## Transport and framing

- **Transport**: UDP. TCP is not used — real-time rendering tolerates packet loss
  better than it tolerates head-of-line blocking.
- **Directions**: two logical streams — Host→IG and IG→Host — typically on different
  UDP ports. Bidirectional on the wire, but each direction has its own framing rules.
- **Datagram structure**: one UDP datagram == one CIGI "message" == one or more
  concatenated packets.
- **First packet in every datagram**:
  - Host→IG: `IG Control` packet (always first)
  - IG→Host: `Start of Frame` packet (always first)
- **Packet concatenation**: after the mandatory first packet, any number of other
  packets follow, each self-describing via its Packet ID and Packet Size fields.
  Readers walk the datagram packet-by-packet using the Size field.
- **MTU**: stay under typical Ethernet MTU (1500 bytes). If more data is needed in a
  frame, the Host must use multiple datagrams — but each still begins with IG Control.

## Frame synchronization

CIGI uses an explicit frame counter for sync, not wall-clock timestamps.

1. Host assembles a frame's worth of state, increments its frame counter, and sends
   an `IG Control` packet carrying that counter plus any state updates.
2. IG renders the frame, then sends a `Start of Frame` packet back with the last
   processed Host frame counter.
3. Host compares the returned counter to its current counter to measure IG latency
   and detect dropped frames.

Typical rates: 30–60 Hz Host→IG update rate, matching IG render rate.

## Byte order

- CIGI 3.x: network byte order (big-endian) for all multi-byte fields.
- CIGI 4.0: adds explicit byte-order negotiation via fields in `IG Control` /
  `Start of Frame`. Verify against the v4.0 ICD before assuming.

Implementations must byteswap on little-endian hosts (x86, most ARM configurations).
In C++ this means `htons`/`htonl`/`htonll` on send, `ntohs`/`ntohl`/`ntohll` on receive,
or a byte-swapping serializer class if working with custom types.

## Packet structure (generic)

Every CIGI packet begins with:

- `Packet ID` — identifies the packet type
  - CIGI 3.x: 1 byte
  - CIGI 4.0: wider (verify in ICD)
- `Packet Size` — total packet length in bytes including the header

After the header, packet-specific fields follow. Reserved/padding bytes are common
at the end of a packet to keep sizes on 8-byte boundaries — critical for 64-bit
alignment of double-precision coordinates.

## Packet categories

The catalog is divided by direction and functional area:

**Host → IG (state push)**
- IG Control — frame counter, scene mode, database selection
- Entity Control — per-entity position, orientation, animation state
- Component Control — generic parameter control on any component
- View Control / View Definition — camera pose and frustum
- Environmental / Atmosphere / Celestial / Weather Control — scene conditions
- Articulated Part Control — moving parts on entities (gear, control surfaces)
- HAT/HOT Request — height-above-terrain / height-of-terrain query
- Line-of-Sight Request — occlusion/intersection query
- Collision Detection Segment/Volume Definition — register collision queries

**IG → Host (feedback / queries)**
- Start of Frame — frame counter echo, IG status
- HAT/HOT Response — query results
- Line-of-Sight Response — query results with intersection point, material code
- Collision Detection Segment/Volume Notification — collision events
- Sensor Response — sensor-specific rendering feedback
- Position Response — entity position after IG-side physics (if used)
- Weather Conditions Response — queried weather state

## Sync groups and scene management

CIGI 3.3+ supports **sync groups** — logical grouping of entities that render
together, useful for multi-channel IGs where different channels must stay in lockstep.
See the version-specific reference file for exact field semantics.

Databases (terrain/scenery sets) are selected via the `IG Control` packet's
`Database Number` field. The IG loads databases asynchronously; the Host must check
`IG Mode` in `Start of Frame` before assuming the selected database is active.

## Common Image Generator Library (CCL / CGL)

SISO publishes an open-source C++ library (commonly called CCL or CGL) that provides
serialization/deserialization for all standard packets. Options for simulator_framework:

1. **Use CCL directly** — fastest path to a conformant implementation. Wrap it in a
   ROS2 node. Downsides: brings in SISO's own build conventions, may lag latest
   spec.
2. **Write a native C++ implementation** — more work, but aligns with the
   standalone-library + ROS2-node pattern used elsewhere in simulator_framework.
   Better for long-term maintenance and for embedded targets.
3. **Hybrid** — use CCL for initial bring-up against a specific IG, then migrate to
   a native implementation once the integration is understood.

Recommended: option 2 for a production FTD/FNPT system. The CIGI spec is stable
enough that owning the serializer is worth the up-front cost.

## Version landscape (practical view)

- **CIGI 3.3**: the "de facto standard" for deployed commercial and military IGs
  through the 2010s. Most IG vendors guarantee 3.3 support.
- **CIGI 4.0**: current major version. Cleaner packet structure, wider packet IDs,
  clearer byte-order handling. Adoption is ongoing — verify with your chosen IG
  vendor before committing.

For a new simulator_framework integration targeting a modern IG (e.g. latest
Mantis, Genesis, VBS Blue IG), start by asking the vendor which CIGI versions they
support and their recommended version for new integrations.

## References inside this skill

- Packet-level detail → `cigi-4.0-packets.md` or `cigi-3.3-packets.md`
- Fitting CIGI into simulator_framework → `integration-patterns.md`
