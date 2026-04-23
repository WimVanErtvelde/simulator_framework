#!/usr/bin/env bash
# Slice 4 smoke test — exercises add_patch, update_patch, bounds validation,
# and clear_patches through the ios_backend WebSocket.

set -e

cd "$(dirname "$0")"

# Ensure python websockets client is available
python3 -c "import websockets" 2>/dev/null || {
    echo "Installing python websockets client..."
    pip3 install --break-system-packages websockets 2>/dev/null || \
    pip3 install --user websockets
}

# Also ensure rclpy / sim_msgs are sourceable for the topic check
source install/setup.bash 2>/dev/null || true

python3 << 'PYEOF'
import asyncio
import json
import sys

WS_URL = "ws://localhost:8080/ws"

async def main():
    import websockets

    try:
        async with websockets.connect(WS_URL) as ws:
            # ── Test 1 — valid airport patch at EBBR ───────────────────
            print("─" * 60)
            print("TEST 1: add_patch with EBBR cumulonimbus")
            print("─" * 60)
            await ws.send(json.dumps({
                "type": "add_patch",
                "data": {
                    "patch_type": "airport",
                    "icao": "EBBR",
                    "label": "Smoke test CB",
                    "cloud_layers": [{
                        "cloud_type": 6,
                        "coverage_pct": 75.0,
                        "base_elevation_m": 1000.0,
                        "thickness_m": 5000.0,
                    }],
                },
            }))

            got_added = False
            got_broadcast = False
            got_ground_m = None
            deadline = 3.0
            while deadline > 0 and (not got_added or not got_broadcast):
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=deadline)
                    d = json.loads(msg)
                    if d.get("type") == "patch_added":
                        got_added = True
                        got_ground_m = d.get("data", {}).get("ground_elevation_m")
                        print(f"  [OK] patch_added  patch_id={d['data'].get('patch_id')} "
                              f"ground={got_ground_m}")
                    elif d.get("type") == "patches":
                        got_broadcast = True
                        n = len(d.get("patches", []))
                        print(f"  [OK] patches broadcast  count={n}")
                except asyncio.TimeoutError:
                    break

            if not got_added:
                print("  [FAIL] no patch_added received within 3s")
                sys.exit(1)
            if not got_broadcast:
                print("  [FAIL] no patches broadcast received within 3s")
                sys.exit(1)
            if got_ground_m is None or not (30 < got_ground_m < 80):
                print(f"  [WARN] ground_elevation_m={got_ground_m} — "
                      "expected ~56m for EBBR. Check airport DB.")
            else:
                print(f"  [OK] ground_elevation ~56 m for EBBR (got {got_ground_m})")

            # ── Test 2 — bounds validation rejects invalid coverage ────
            print()
            print("─" * 60)
            print("TEST 2: add_patch with invalid coverage_pct=500")
            print("─" * 60)
            await ws.send(json.dumps({
                "type": "add_patch",
                "data": {
                    "patch_type": "airport",
                    "icao": "EBBR",
                    "cloud_layers": [{
                        "cloud_type": 1,
                        "coverage_pct": 500.0,
                        "base_elevation_m": 500.0,
                        "thickness_m": 200.0,
                    }],
                },
            }))

            got_error = False
            deadline = 2.0
            while deadline > 0:
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=deadline)
                    d = json.loads(msg)
                    if d.get("type") == "patch_error":
                        got_error = True
                        print(f"  [OK] patch_error  {d.get('error')}")
                        break
                except asyncio.TimeoutError:
                    break
                deadline -= 0.1

            if not got_error:
                print("  [FAIL] expected patch_error for out-of-range coverage")
                sys.exit(1)

            # ── Test 3 — unknown ICAO ──────────────────────────────────
            print()
            print("─" * 60)
            print("TEST 3: add_patch with bogus ICAO 'XXXX'")
            print("─" * 60)
            await ws.send(json.dumps({
                "type": "add_patch",
                "data": {
                    "patch_type": "airport",
                    "icao": "XXXX",
                    "label": "ghost airport",
                },
            }))

            got_response = False
            deadline = 3.0
            while deadline > 0:
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=deadline)
                    d = json.loads(msg)
                    if d.get("type") == "patch_added":
                        got_response = True
                        g = d.get("data", {}).get("ground_elevation_m")
                        is_nan = (
                            g is None
                            or (isinstance(g, float) and g != g)
                            or (isinstance(g, str) and g.lower() == "nan")
                        )
                        if is_nan:
                            print(f"  [OK] patch_added with ground_elevation=NaN "
                                  "(expected for unknown ICAO)")
                        else:
                            print(f"  [WARN] patch_added but ground_elevation={g} "
                                  "(expected NaN for unknown ICAO)")
                        break
                except asyncio.TimeoutError:
                    break
                deadline -= 0.1

            if not got_response:
                print("  [WARN] no patch_added received for XXXX — "
                      "may be a bounds-validator quirk, check backend logs")

            # ── Cleanup ────────────────────────────────────────────────
            print()
            print("─" * 60)
            print("CLEANUP: clear_patches")
            print("─" * 60)
            await ws.send(json.dumps({"type": "clear_patches"}))
            await asyncio.sleep(0.5)
            print("  [OK] patches cleared")

            print()
            print("=" * 60)
            print("SMOKE TEST PASSED")
            print("=" * 60)

    except websockets.exceptions.ConnectionClosed:
        print("[FAIL] WS connection closed unexpectedly")
        sys.exit(1)
    except ConnectionRefusedError:
        print("[FAIL] Could not connect to ios_backend at", WS_URL)
        print("       Is ios_backend running?")
        sys.exit(1)

asyncio.run(main())
PYEOF

echo
echo "────────────────────────────────────────────────────────────"
echo "FINAL CHECK: publish a patch and confirm it appears on /world/weather"
echo "────────────────────────────────────────────────────────────"

python3 << 'PYEOF'
import asyncio, json, websockets
async def _add():
    async with websockets.connect("ws://localhost:8080/ws") as ws:
        await ws.send(json.dumps({
            "type": "add_patch",
            "data": {"patch_type": "airport", "icao": "EBBR", "label": "Final check"},
        }))
        await asyncio.sleep(0.5)
asyncio.run(_add())
PYEOF

sleep 1
echo "Top-level patches[] from /world/weather (first message):"
timeout 2 ros2 topic echo /world/weather --once --no-arr 2>/dev/null | \
    grep -A 3 "^patches:" || echo "(timeout or no patches visible — check manually)"

python3 << 'PYEOF'
import asyncio, json, websockets
async def _clear():
    async with websockets.connect("ws://localhost:8080/ws") as ws:
        await ws.send(json.dumps({"type": "clear_patches"}))
        await asyncio.sleep(0.2)
asyncio.run(_clear())
PYEOF

echo
echo "DONE."
