#!/usr/bin/env python3
"""
SRTM3 / NASADEM / Copernicus DEM Downloader for NavSim
=======================================================
Downloads terrain tiles and saves them as SRTM3-compatible HGT files.

Tries the following data sources in order:
  1. NASA SRTMGL3.003 via LP DAAC Cloud   (requires NASA EarthData account)
  2. NASA NASADEM_HGT.001 via CMR search   (requires NASA EarthData account)
  3. Copernicus DEM GLO-30 via AWS          (FREE, no account needed!)

REQUIREMENTS:
    pip install requests
    pip install tifffile numpy    ← only needed if Copernicus fallback is used

SETUP (for NASA sources):
    Register free at https://urs.earthdata.nasa.gov/users/new
    Then run:
        python download_srtm3.py --user YOUR_USERNAME --password YOUR_PASSWORD

    Without --user/--password the script still tries Copernicus (no auth needed).

    NOTE: After registering at EarthData, you must also authorize LP DAAC:
        https://urs.earthdata.nasa.gov
        → Profile → Applications → Authorized Apps
        → Approve "LP DAAC - Data Pool"

REGION OPTIONS (--region):
    europe    ~500 land tiles, ~1.5 GB  (recommended for NavSim EURAMEC coverage)
    world     ~15000 tiles,    ~45 GB

OUTPUT:
    NavSim/terrain/srtm3/N52E004.hgt  (SRTM3-format HGT, one file per 1°x1° tile)
"""

import os, sys, io, zipfile, argparse, threading
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

try:
    import requests
except ImportError:
    print("ERROR: 'requests' library not found.  Run: pip install requests")
    sys.exit(1)

# ── Configuration ──────────────────────────────────────────────────────────────
EARTHDATA_USER = os.environ.get("EARTHDATA_USER", "")
EARTHDATA_PASS = os.environ.get("EARTHDATA_PASS", "")

URS_URL = "https://urs.earthdata.nasa.gov"
CMR_URL = "https://cmr.earthdata.nasa.gov/search/granules.json"
CMR_HEADERS = {"User-Agent": "NavSim-Terrain-Downloader/3.0 (navsimsim@example.com)"}

OUT_DIR = Path(__file__).parent / "terrain" / "srtm3"

# ── Region presets ─────────────────────────────────────────────────────────────
REGIONS = {
    "europe":   (34, 72,  -25, 46),
    "africa":   (-35, 38, -20, 52),
    "namerica": (15,  61, -170, -50),
    "samerica": (-56, 15,  -82, -33),
    "asia":     (0,   55,   55, 145),
    "oceania":  (-45, 10,  110, 180),
    "euramec":  (34, 72,  -25, 46),   # alias — EURAMEC navaid coverage
    "world":    (-56, 61, -180, 180),
}

# ── Tile helpers ───────────────────────────────────────────────────────────────
def tile_name(lat: int, lon: int) -> str:
    ns = "N" if lat >= 0 else "S"
    ew = "E" if lon >= 0 else "W"
    return f"{ns}{abs(lat):02d}{ew}{abs(lon):03d}"

def build_tile_list(region_key: str):
    lat_min, lat_max, lon_min, lon_max = REGIONS[region_key]
    return [(lat, lon)
            for lat in range(lat_min, lat_max)
            for lon in range(lon_min, lon_max)]

# ── EarthData Bearer Token ─────────────────────────────────────────────────────
def get_earthdata_token(username: str, password: str) -> str | None:
    """
    Fetch a Bearer token from NASA EarthData URS.
    NASA LP DAAC Cloud (data.lpdaac.earthdatacloud.nasa.gov) requires Bearer
    token auth — basic auth / cookie auth does NOT work for cloud endpoints.
    """
    try:
        # Try to get an existing token first
        r = requests.get(f"{URS_URL}/api/users/tokens",
                         auth=(username, password), timeout=30)
        if r.status_code == 200:
            tokens = r.json()
            if tokens:
                exp = tokens[0].get("expiration_date", "unknown")
                print(f"    Using existing EarthData token (expires {exp})")
                return tokens[0]["access_token"]
    except Exception:
        pass

    try:
        # No existing token — create a new one
        r = requests.post(f"{URS_URL}/api/users/token",
                          auth=(username, password), timeout=30)
        if r.status_code == 200:
            tok = r.json().get("access_token")
            if tok:
                print(f"    Created new EarthData token")
                return tok
        print(f"    Token creation returned HTTP {r.status_code}")
    except Exception as e:
        print(f"    Token creation error: {e}")

    return None

# ── CMR URL discovery ──────────────────────────────────────────────────────────
def cmr_find_url(tile: str, short_name: str, version: str | None = None) -> str | None:
    """
    Query NASA CMR (public API, no auth needed) for a 1°×1° tile.
    Returns the first plausible .zip or .hgt download URL found, or None.
    """
    lat = int(tile[1:3]) * (1 if tile[0] == "N" else -1)
    lon = int(tile[4:7]) * (1 if tile[3] == "E" else -1)

    params: dict = {
        "short_name":   short_name,
        "bounding_box": f"{lon+0.1},{lat+0.1},{lon+0.9},{lat+0.9}",
        "page_size":    10,
    }
    if version:
        params["version"] = version

    try:
        # NOTE: plain requests.get — NOT the authed session — CMR is public
        r = requests.get(CMR_URL, params=params, headers=CMR_HEADERS, timeout=30)
        if r.status_code != 200:
            return None
        entries = r.json().get("feed", {}).get("entry", [])
        for entry in entries:
            for link in entry.get("links", []):
                href = link.get("href", "")
                if not href.startswith("http"):
                    continue
                if "browse" in href.lower() or "opendap" in href.lower():
                    continue
                if href.endswith(".zip") or href.endswith(".hgt"):
                    return href
    except Exception:
        pass

    return None

# ── Copernicus DEM (no auth, AWS public) ──────────────────────────────────────
def copernicus_url(lat: int, lon: int) -> str:
    """Copernicus GLO-30 GeoTIFF URL for this 1°×1° tile (public S3, no auth)."""
    ns = f"N{lat:02d}"  if lat >= 0 else f"S{abs(lat):02d}"
    ew = f"E{lon:03d}"  if lon >= 0 else f"W{abs(lon):03d}"
    name = f"Copernicus_DSM_COG_10_{ns}_00_{ew}_00_DEM"
    return f"https://copernicus-dem-30m.s3.amazonaws.com/{name}/{name}.tif"

def geotiff_to_hgt(tiff_bytes: bytes, hgt_path: Path) -> tuple[bool, str]:
    """
    Convert a Copernicus DEM GeoTIFF to SRTM3-compatible HGT format.

    Copernicus GLO-30: 3600×3600 pixels per 1°×1° tile (1 arc-sec, 30m)
    SRTM3 HGT target: 1201×1201 pixels per 1°×1° tile (3 arc-sec, 90m)

    Downsample by factor 3: 3600 → 1200, then add edge pixel → 1201.
    """
    try:
        import tifffile
        import numpy as np
    except ImportError:
        return False, ("tifffile/numpy not installed — run:  pip install tifffile numpy\n"
                       "Without these, Copernicus DEM fallback is unavailable.")

    try:
        arr = tifffile.imread(io.BytesIO(tiff_bytes)).astype(np.float32)
    except Exception as e:
        return False, f"TIFF read error: {e}"

    h, w = arr.shape[:2]

    # Downsample from 3600×3600 (1 arc-sec) to 1200×1200 (3 arc-sec)
    if h >= 3000 and w >= 3000:
        step = round(h / 1200)
        arr = arr[::step, ::step]
        arr = arr[:1200, :1200]   # trim any rounding excess

    # Add edge overlap row/column to reach 1201×1201 (SRTM3 convention)
    arr = np.vstack([arr, arr[-1:, :]])
    arr = np.hstack([arr, arr[:, -1:]])

    # Replace Copernicus no-data value (-32767.0 / very large) with SRTM no-data
    arr[arr < -32000] = -32768

    # Write big-endian int16 (SRTM3 / HGT format)
    hgt_bytes = arr.clip(-32768, 32767).astype(">i2").tobytes()
    hgt_path.write_bytes(hgt_bytes)
    return True, ""

# ── Session factory ────────────────────────────────────────────────────────────
def make_bearer_session(token: str) -> requests.Session:
    s = requests.Session()
    s.headers["Authorization"] = f"Bearer {token}"
    return s

def make_basic_session(username: str, password: str) -> requests.Session:
    s = requests.Session()
    s.auth = (username, password)
    return s

# ── Strategy probe ─────────────────────────────────────────────────────────────
TEST_TILE_LAT, TEST_TILE_LON = 51, 4   # N51E004 — Belgium/Netherlands, always exists

def probe_lpdaac_cloud(session: requests.Session) -> bool:
    name = tile_name(TEST_TILE_LAT, TEST_TILE_LON)
    url  = (f"https://data.lpdaac.earthdatacloud.nasa.gov"
            f"/lp-prod-protected/SRTMGL3.003/{name}/{name}.SRTMGL3.hgt.zip")
    return _probe_zip_url(session, name, url, "SRTMGL3.003 LP DAAC Cloud")

NASADEM_DIRECT_URL = (
    "https://data.lpdaac.earthdatacloud.nasa.gov"
    "/lp-prod-protected/NASADEM_HGT.001"
    "/NASADEM_HGT_{n}/NASADEM_HGT_{n}.zip"
)

def nasadem_url(name: str) -> str:
    """Direct NASADEM_HGT.001 URL — no CMR query needed."""
    n = name.lower()   # e.g. N51E004 → n51e004
    return NASADEM_DIRECT_URL.format(n=n)

def probe_nasadem_direct(session: requests.Session) -> bool:
    name = tile_name(TEST_TILE_LAT, TEST_TILE_LON)
    url  = nasadem_url(name)
    return _probe_zip_url(session, name, url, "NASADEM_HGT.001 (direct URL)")

def probe_nasadem_cmr(session: requests.Session) -> str | None:
    """Fallback: use CMR to discover URL if direct pattern fails."""
    name = tile_name(TEST_TILE_LAT, TEST_TILE_LON)
    print(f"    Trying NASADEM_HGT.001 (via CMR bounding-box search)...")
    url = cmr_find_url(name, "NASADEM_HGT", "001")
    if url is None:
        print(f"      CMR returned no results for NASADEM_HGT / bounding box of {name}")
        return None
    print(f"      CMR found URL: {url}")
    ok = _probe_zip_url(session, name, url, "NASADEM_HGT.001")
    return url if ok else None

def probe_srtmgl3_cmr(session: requests.Session) -> str | None:
    name = tile_name(TEST_TILE_LAT, TEST_TILE_LON)
    print(f"    Trying SRTMGL3.003 (via CMR bounding-box search)...")
    url = cmr_find_url(name, "SRTMGL3", "003")
    if url is None:
        print(f"      CMR returned no results for SRTMGL3 / bounding box of {name}")
        return None
    print(f"      CMR found URL: {url}")
    ok = _probe_zip_url(session, name, url, "SRTMGL3.003 via CMR")
    return url if ok else None

def probe_copernicus() -> bool:
    name = tile_name(TEST_TILE_LAT, TEST_TILE_LON)
    url  = copernicus_url(TEST_TILE_LAT, TEST_TILE_LON)
    print(f"    Trying Copernicus DEM GLO-30 (public AWS, no auth)...")
    print(f"      URL: {url}")
    try:
        r = requests.get(url, timeout=60, stream=True)
    except requests.RequestException as e:
        print(f"      Connection error: {e}")
        return False

    if r.status_code == 404:
        print(f"      404 — tile not in Copernicus coverage")
        return False
    if r.status_code != 200:
        print(f"      HTTP {r.status_code}")
        return False

    # Check content type
    ct = r.headers.get("Content-Type", "")
    if "image/tiff" in ct or "application/octet" in ct or "binary" in ct or len(r.content) > 10_000:
        # Try conversion
        tmp = Path("/tmp/probe_copernicus.hgt")
        ok, err = geotiff_to_hgt(r.content, tmp)
        if ok:
            sz = tmp.stat().st_size // 1024
            print(f"      ✓ SUCCESS — {len(r.content)//1024} KB TIFF → {sz} KB HGT")
            tmp.unlink(missing_ok=True)
            return True
        else:
            print(f"      Conversion failed: {err}")
            return False
    else:
        print(f"      Unexpected content-type: {ct[:80]}")
        return False

def _probe_zip_url(session: requests.Session, name: str, url: str, label: str) -> bool:
    print(f"    Trying {label}...")
    print(f"      URL: {url}")
    try:
        r = session.get(url, timeout=60)
    except requests.RequestException as e:
        print(f"      Connection error: {e}")
        return False

    if r.status_code == 401:
        print(f"      401 Unauthorized")
        return False
    if r.status_code == 403:
        print(f"      403 Forbidden — approve LP DAAC at earthdata.nasa.gov")
        return False
    if r.status_code == 404:
        print(f"      404 Not Found")
        return False
    if r.status_code != 200:
        print(f"      HTTP {r.status_code}")
        return False

    ct = r.headers.get("Content-Type", "")
    if "html" in ct:
        print(f"      Got HTML login page — auth not working")
        return False

    try:
        with zipfile.ZipFile(io.BytesIO(r.content)) as zf:
            hgts = [n for n in zf.namelist() if n.lower().endswith(".hgt")]
            if hgts:
                print(f"      ✓ SUCCESS — {len(r.content)//1024} KB → contains {hgts[0]}")
                return True
            print(f"      Zip has no .hgt files")
            return False
    except zipfile.BadZipFile:
        print(f"      Not a valid zip (Content-Type: {ct[:60]})")
        return False

# ── Download one tile ──────────────────────────────────────────────────────────
def download_tile_zip(session: requests.Session, url: str,
                      hgt_path: Path, skip_path: Path,
                      stats: dict, lock: threading.Lock, name: str):
    try:
        r = session.get(url, timeout=90)
    except requests.RequestException as e:
        with lock:
            stats["fail"] += 1; stats["errors"].append(f"{name}: {e}")
        return

    if r.status_code == 404:
        skip_path.touch(); _inc(stats, lock, "ocean"); return
    if r.status_code != 200:
        with lock:
            stats["fail"] += 1; stats["errors"].append(f"{name}: HTTP {r.status_code}")
        return
    if "html" in r.headers.get("Content-Type", ""):
        with lock:
            stats["fail"] += 1; stats["errors"].append(f"{name}: HTML (auth expired)")
        return

    try:
        with zipfile.ZipFile(io.BytesIO(r.content)) as zf:
            hgt_names = [n for n in zf.namelist() if n.lower().endswith(".hgt")]
            if not hgt_names:
                with lock:
                    stats["fail"] += 1; stats["errors"].append(f"{name}: no .hgt in zip")
                return
            hgt_path.write_bytes(zf.read(hgt_names[0]))
            _inc(stats, lock, "ok")
    except zipfile.BadZipFile:
        with lock:
            stats["fail"] += 1; stats["errors"].append(f"{name}: bad zip")

def download_tile_copernicus(lat: int, lon: int,
                             hgt_path: Path, skip_path: Path,
                             stats: dict, lock: threading.Lock, name: str):
    url = copernicus_url(lat, lon)
    try:
        r = requests.get(url, timeout=90)
    except requests.RequestException as e:
        with lock:
            stats["fail"] += 1; stats["errors"].append(f"{name}: {e}")
        return

    if r.status_code == 404:
        skip_path.touch(); _inc(stats, lock, "ocean"); return
    if r.status_code != 200:
        with lock:
            stats["fail"] += 1; stats["errors"].append(f"{name}: HTTP {r.status_code}")
        return

    ok, err = geotiff_to_hgt(r.content, hgt_path)
    if ok:
        _inc(stats, lock, "ok")
    else:
        with lock:
            stats["fail"] += 1; stats["errors"].append(f"{name}: {err}")

def download_tile(args_tuple):
    lat, lon, out_dir, stats, lock, mode, session, cmr_dataset = args_tuple
    name      = tile_name(lat, lon)
    hgt_path  = out_dir / f"{name}.hgt"
    skip_path = out_dir / f"{name}.skip"

    if hgt_path.exists() and hgt_path.stat().st_size > 100_000:
        _inc(stats, lock, "skip"); return
    if skip_path.exists():
        _inc(stats, lock, "skip"); return

    if mode == "lpdaac_cloud":
        url = (f"https://data.lpdaac.earthdatacloud.nasa.gov"
               f"/lp-prod-protected/SRTMGL3.003/{name}/{name}.SRTMGL3.hgt.zip")
        download_tile_zip(session, url, hgt_path, skip_path, stats, lock, name)

    elif mode == "nasadem_direct":
        url = nasadem_url(name)
        download_tile_zip(session, url, hgt_path, skip_path, stats, lock, name)

    elif mode == "cmr":
        sn, ver = cmr_dataset
        url = cmr_find_url(name, sn, ver)
        if url is None:
            skip_path.touch(); _inc(stats, lock, "ocean"); return
        download_tile_zip(session, url, hgt_path, skip_path, stats, lock, name)

    elif mode == "copernicus":
        download_tile_copernicus(lat, lon, hgt_path, skip_path, stats, lock, name)

def _inc(stats, lock, key):
    with lock: stats[key] += 1

# ── Progress ───────────────────────────────────────────────────────────────────
def show_progress(stats, total, done):
    pct    = done / total * 100 if total else 0
    filled = int(40 * done / total) if total else 0
    bar    = "█" * filled + "░" * (40 - filled)
    print(f"\r  [{bar}] {done:5d}/{total}  {pct:5.1f}%  "
          f"✓{stats['ok']}  skip{stats['skip']}  "
          f"ocean{stats['ocean']}  ✗{stats['fail']}",
          end="", flush=True)

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(
        description="Download SRTM3-compatible terrain tiles for NavSim")
    ap.add_argument("--user",     default=EARTHDATA_USER,
                    help="NASA EarthData username (not needed for Copernicus fallback)")
    ap.add_argument("--password", default=EARTHDATA_PASS,
                    help="NASA EarthData password")
    ap.add_argument("--region",   default="europe", choices=list(REGIONS.keys()))
    ap.add_argument("--workers",  type=int, default=4)
    args = ap.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    print("=" * 65)
    print("NavSim Terrain Downloader  (SRTM3 / NASADEM / Copernicus DEM)")
    print("=" * 65)
    print(f"  Region  : {args.region}")
    print(f"  Output  : {OUT_DIR}")
    print(f"  Workers : {args.workers}")
    print()

    # ── Step 1: authenticate and get Bearer token ──────────────────────────────
    token   = None
    session = None

    if args.user and args.password:
        print("Getting NASA EarthData Bearer token...")
        token = get_earthdata_token(args.user, args.password)
        if token:
            session = make_bearer_session(token)
        else:
            print("  WARNING: could not get a token — will skip NASA sources")
            print("  Tip: After creating your account, wait a few minutes, then retry.")
            print("       Also ensure you have approved LP DAAC at:")
            print("       https://urs.earthdata.nasa.gov → Profile → Authorized Apps")
    else:
        print("No EarthData credentials provided — will try Copernicus DEM only.")
    print()

    # ── Step 2: probe available sources ───────────────────────────────────────
    print("Probing available data sources with test tile N51E004...")
    mode        = None
    cmr_dataset = None

    if session:
        if probe_lpdaac_cloud(session):
            mode = "lpdaac_cloud"

        if mode is None:
            # Try NASADEM direct URL first (fastest — no per-tile CMR lookup)
            if probe_nasadem_direct(session):
                mode = "nasadem_direct"

        if mode is None:
            # Fall back to CMR-discovered URL
            url = probe_nasadem_cmr(session)
            if url:
                mode = "cmr"
                cmr_dataset = ("NASADEM_HGT", "001")

        if mode is None:
            url = probe_srtmgl3_cmr(session)
            if url:
                mode = "cmr"
                cmr_dataset = ("SRTMGL3", "003")

    if mode is None:
        if probe_copernicus():
            mode = "copernicus"

    if mode is None:
        print()
        print("ERROR: No working data source found.")
        print()
        if args.user:
            print("NASA sources failed. Checklist:")
            print("  1. Verify credentials at https://urs.earthdata.nasa.gov")
            print("  2. Approve LP DAAC: Profile → Applications → Authorized Apps")
            print("  3. Wait a few minutes after first registration before retrying")
        print()
        print("For Copernicus (no account needed), ensure these packages are installed:")
        print("   pip install tifffile numpy")
        sys.exit(1)

    print(f"\n  Selected source: {mode}")

    # CMR-per-tile is slow → reduce parallelism; direct modes can use full concurrency
    max_workers = args.workers
    if mode == "cmr":
        print("  NOTE: CMR per-tile URL lookup is slow — limiting to 2 workers.")
        max_workers = min(max_workers, 2)
    elif mode in ("nasadem_direct", "lpdaac_cloud"):
        print(f"  Direct URL mode — using {max_workers} workers.")

    # ── Step 3: download ──────────────────────────────────────────────────────
    tiles = build_tile_list(args.region)
    total = len(tiles)
    print(f"\nStarting download of {total:,} candidate tiles "
          f"(ocean/void tiles auto-skipped)...\n")

    stats = {"ok": 0, "skip": 0, "ocean": 0, "fail": 0, "errors": []}
    lock  = threading.Lock()
    done  = 0

    task_args = [
        (lat, lon, OUT_DIR, stats, lock, mode, session, cmr_dataset)
        for lat, lon in tiles
    ]

    with ThreadPoolExecutor(max_workers=max_workers) as pool:
        futures = {pool.submit(download_tile, a): a for a in task_args}
        for _ in as_completed(futures):
            done += 1
            show_progress(stats, total, done)

    print("\n")
    print("=" * 65)
    print("Done!")
    print(f"  Downloaded : {stats['ok']:,} tiles")
    print(f"  Skipped    : {stats['skip']:,} already on disk")
    print(f"  Ocean/void : {stats['ocean']:,} tiles (no data)")
    print(f"  Failed     : {stats['fail']:,} tiles")

    if stats["errors"]:
        preview = stats["errors"][:20]
        print(f"\nFirst {len(preview)} errors:")
        for e in preview: print(f"  {e}")
        if len(stats["errors"]) > 20:
            err_log = OUT_DIR.parent / "download_errors.txt"
            err_log.write_text("\n".join(stats["errors"]))
            print(f"\n  Full error log: {err_log}")

    hgts = list(OUT_DIR.glob("*.hgt"))
    total_mb = sum(f.stat().st_size for f in hgts) / 1024**2
    print(f"\n  Total on disk : {total_mb:.0f} MB  ({len(hgts):,} HGT files)")
    print(f"  Location      : {OUT_DIR}")


if __name__ == "__main__":
    main()
