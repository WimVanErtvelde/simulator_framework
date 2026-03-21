#!/usr/bin/env python3
"""
maxpool_hgt.py — downsample NASADEM 1 arc-sec HGT tiles to 6 arc-sec
                  using MAX-POOLING (safe for LOS checking in mountain terrain).

Source : srtm3_hires/  — 3601×3601 big-endian int16 (1 arc-sec, ~25 MB each)
Output : srtm3/        — 601×601  big-endian int16 (6 arc-sec, ~700 KB each)

Why max-pool (not pixel-skip)?
  Pixel-skip can miss ridge peaks by hundreds of metres — a helicopter in a Swiss
  valley would receive a false "LOS clear" from a navaid on the other side of a ridge.
  Max-pool always keeps the HIGHEST pixel in each 6×6 block, so terrain is never
  underestimated.  Valley floors may read ~20 m too high on average — acceptable.

Resolution:
  1 arc-sec  = ~30 m horizontal  (source)
  6 arc-sec  = ~180 m horizontal (output)

Usage:
  python maxpool_hgt.py [--src DIR] [--dst DIR] [--jobs N] [--dry-run]

  --src   source directory with 1 arc-sec .hgt tiles  (default: terrain/srtm3_hires)
  --dst   output directory for 6 arc-sec .hgt tiles   (default: terrain/srtm3)
  --jobs  parallel worker threads                      (default: 4)
  --dry-run  print what would be done, write nothing
"""

import argparse
import os
import sys
import numpy as np
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading

# ── Constants ────────────────────────────────────────────────────────────────
ROWS_IN  = 3601   # 1 arc-sec tile: 3601 × 3601
COLS_IN  = 3601
ROWS_OUT = 601    # 6 arc-sec tile: 601 × 601
COLS_OUT = 601
BLOCK    = 6      # 6×6 input pixels → 1 output pixel
VOID     = -32768 # HGT void value (ocean / no data)

# ── Worker ───────────────────────────────────────────────────────────────────
_print_lock = threading.Lock()

def log(msg):
    with _print_lock:
        print(msg, flush=True)

def maxpool_tile(src_path: Path, dst_path: Path, dry_run: bool) -> str:
    """Read one 3601×3601 tile, max-pool to 601×601, write output."""
    name = src_path.name

    # Skip if output already exists and is the right size
    expected = ROWS_OUT * COLS_OUT * 2
    if dst_path.exists() and dst_path.stat().st_size == expected:
        return f"  Already done : {name}"

    if dry_run:
        return f"  Would convert: {name}"

    # ── Read ─────────────────────────────────────────────────────────────────
    raw = src_path.read_bytes()
    if len(raw) != ROWS_IN * COLS_IN * 2:
        return f"  SKIP (wrong size {len(raw)}): {name}"

    arr = np.frombuffer(raw, dtype='>i2').reshape(ROWS_IN, COLS_IN).copy()

    # ── Max-pool ─────────────────────────────────────────────────────────────
    # Treat void (-32768) as a very low value so real terrain always wins.
    # Replace void with -32767 temporarily so max() still picks real data.
    void_mask = (arr == VOID)
    arr[void_mask] = np.iinfo(np.int16).min + 1  # -32767

    # Trim to 3600×3600 (divisible by 6) then reshape for vectorised max.
    # Last row/col (index 3600) is handled separately — it's the same lat/lon
    # as the neighbouring tile's first row/col (overlap pixel in HGT format).
    trimmed = arr[:3600, :3600]                        # 3600×3600
    blocks  = trimmed.reshape(600, 6, 600, 6)          # (600,6,600,6)
    pooled  = blocks.max(axis=(1, 3)).astype(np.int16) # 600×600

    # Build 601×601 output:
    #   rows 0-599 / cols 0-599 → max-pooled blocks
    #   row 600                 → max-pool of last 6 rows  (rows 3595-3600)
    #   col 600                 → max-pool of last 6 cols  (cols 3595-3600)
    #   [600,600]               → single pixel arr[3600,3600]
    out = np.empty((ROWS_OUT, COLS_OUT), dtype=np.int16)
    out[:600, :600] = pooled

    # Last row: max of rows 3595:3601 for each of the 600 column blocks,
    # then the corner pixel
    last_rows = arr[3595:3601, :]                      # 6×3601
    for ci in range(600):
        out[600, ci] = last_rows[:, ci*6:(ci+1)*6].max()
    out[600, 600] = arr[3600, 3600]

    # Last col: max of cols 3595:3601 for each of the 600 row blocks
    last_cols = arr[:, 3595:3601]                      # 3601×6
    for ri in range(600):
        out[ri, 600] = last_cols[ri*6:(ri+1)*6, :].max()

    # Restore void: output pixel is void only if ALL 36 source pixels were void
    # We approximate: if pooled value is still -32767 → all were void → set VOID
    out[out == np.iinfo(np.int16).min + 1] = VOID

    # ── Write (big-endian) ────────────────────────────────────────────────────
    dst_path.parent.mkdir(parents=True, exist_ok=True)
    dst_path.write_bytes(out.astype('>i2').tobytes())

    src_mb = src_path.stat().st_size / 1_048_576
    dst_kb = dst_path.stat().st_size / 1024
    return f"  Converted : {name}  ({src_mb:.0f} MB → {dst_kb:.0f} KB)"

# ── Main ─────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--src',     default='terrain/srtm3_hires',
                        help='Source directory (1 arc-sec .hgt tiles)')
    parser.add_argument('--dst',     default='terrain/srtm3',
                        help='Output directory (6 arc-sec .hgt tiles)')
    parser.add_argument('--jobs',    type=int, default=4,
                        help='Parallel worker threads (default: 4)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Show what would be done without writing files')
    args = parser.parse_args()

    src_dir = Path(args.src)
    dst_dir = Path(args.dst)

    if not src_dir.is_dir():
        print(f"ERROR: source directory not found: {src_dir}", file=sys.stderr)
        sys.exit(1)

    tiles = sorted(src_dir.glob('*.hgt'))
    if not tiles:
        print(f"ERROR: no .hgt files found in {src_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Source : {src_dir}  ({len(tiles)} tiles)")
    print(f"Output : {dst_dir}")
    print(f"Method : 6×6 max-pool  (1 arc-sec → 6 arc-sec)")
    print(f"Workers: {args.jobs}")
    if args.dry_run:
        print("DRY RUN — no files will be written")
    print()

    done = 0
    skipped = 0
    errors = 0

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(maxpool_tile, t, dst_dir / t.name, args.dry_run): t
            for t in tiles
        }
        for fut in as_completed(futures):
            try:
                msg = fut.result()
                log(msg)
                if 'Already' in msg:
                    skipped += 1
                elif 'SKIP' in msg or 'ERROR' in msg:
                    errors += 1
                else:
                    done += 1
            except Exception as e:
                tile = futures[fut]
                log(f"  ERROR {tile.name}: {e}")
                errors += 1

    total_src_gb = sum(t.stat().st_size for t in tiles) / 1e9
    if not args.dry_run and dst_dir.exists():
        dst_tiles = list(dst_dir.glob('*.hgt'))
        total_dst_gb = sum(t.stat().st_size for t in dst_tiles) / 1e9
    else:
        total_dst_gb = len(tiles) * ROWS_OUT * COLS_OUT * 2 / 1e9

    print()
    print(f"Converted : {done}")
    print(f"Skipped   : {skipped}  (already up to date)")
    print(f"Errors    : {errors}")
    print(f"Source    : {total_src_gb:.1f} GB")
    if not args.dry_run:
        print(f"Output    : {total_dst_gb:.2f} GB")
        print(f"Saved     : ~{total_src_gb - total_dst_gb:.1f} GB  "
              f"({(1 - total_dst_gb/total_src_gb)*100:.0f}% reduction)")

if __name__ == '__main__':
    main()
