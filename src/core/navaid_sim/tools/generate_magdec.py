#!/usr/bin/env python3
"""
Generate a magnetic declination grid CSV from WMM coefficients.

Output: magdec.csv  (lat,lon,declination)
  lat  :  +90.0 … −90.0  (north to south, 0.5° steps)
  lon  : −180.0 … +180.0 (west to east,   0.5° steps)
  dec  :  degrees (positive = east)

The embedded coefficients are WMM2020 (valid 2020.0–2025.0).
To update: replace the WMM_COEFFICIENTS block with WMM2025 data from
  https://www.ncei.noaa.gov/products/world-magnetic-model/wmm-coefficients
and change EPOCH to 2025.0.

Usage:
  python generate_magdec.py                       # default: epoch midpoint, 0.5° grid
  python generate_magdec.py --year 2024.5         # specific decimal year
  python generate_magdec.py --step 1.0            # 1° grid
  python generate_magdec.py --cof WMM.COF         # load coefficients from file
  python generate_magdec.py --out magdec_2025.csv # custom output filename
"""

import math
import argparse
import sys
import os

# ─────────────────────────────────────────────────────────────────────────────
# WMM2020 spherical harmonic coefficients  (epoch 2020.0, valid through 2025.0)
# Format: (n, m, gnm, hnm, dgnm, dhnm)
#   n,m  = degree and order
#   gnm  = main field g coefficient (nT)
#   hnm  = main field h coefficient (nT)
#   dgnm = secular variation dg/dt  (nT/yr)
#   dhnm = secular variation dh/dt  (nT/yr)
# ─────────────────────────────────────────────────────────────────────────────

EPOCH = 2020.0

WMM_COEFFICIENTS = [
    # n  m      gnm       hnm      dgnm     dhnm
    (1, 0, -29404.5,      0.0,     6.7,     0.0),
    (1, 1,  -1450.7,   4652.9,     7.7,   -25.1),
    (2, 0,  -2500.0,      0.0,   -11.5,     0.0),
    (2, 1,   2982.0,  -2991.6,    -7.1,   -30.2),
    (2, 2,   1676.8,   -734.8,    -2.2,   -23.9),
    (3, 0,   1363.9,      0.0,     2.8,     0.0),
    (3, 1,  -2381.0,    -82.2,    -6.2,     5.7),
    (3, 2,   1236.2,    241.8,     3.4,    -1.0),
    (3, 3,    525.7,   -542.9,   -12.2,     1.1),
    (4, 0,    903.1,      0.0,    -1.1,     0.0),
    (4, 1,    809.4,    282.0,    -1.6,     6.9),
    (4, 2,     86.2,   -158.4,    -6.0,     2.5),
    (4, 3,   -309.4,    199.8,     5.4,     3.7),
    (4, 4,     47.9,   -350.1,    -5.5,    -5.6),
    (5, 0,   -234.4,      0.0,    -0.3,     0.0),
    (5, 1,    363.1,     47.7,     0.1,     0.1),
    (5, 2,    187.8,    208.4,    -0.7,     2.5),
    (5, 3,   -140.7,   -121.3,     0.1,    -0.9),
    (5, 4,   -151.2,     32.2,     1.2,     3.0),
    (5, 5,     13.7,     99.1,     1.0,     0.5),
    (6, 0,     65.9,      0.0,    -0.6,     0.0),
    (6, 1,     65.6,    -19.1,    -0.6,     0.0),
    (6, 2,     73.0,     25.0,     0.5,     1.4),
    (6, 3,   -121.5,     52.7,     1.4,    -1.2),
    (6, 4,    -36.2,    -64.4,    -1.4,     0.0),
    (6, 5,     13.5,      9.0,     0.0,     0.3),
    (6, 6,    -64.7,     68.1,     0.8,     1.7),
    (7, 0,     80.6,      0.0,    -0.1,     0.0),
    (7, 1,    -76.8,    -51.4,    -0.3,     0.5),
    (7, 2,     -8.3,    -16.8,     0.1,     0.6),
    (7, 3,     56.5,      2.3,     0.7,    -0.7),
    (7, 4,     15.8,     23.5,     0.2,    -0.2),
    (7, 5,      6.4,     -2.2,    -0.5,    -1.2),
    (7, 6,     -7.2,    -27.2,    -0.8,     0.2),
    (7, 7,      9.8,     -1.8,     1.0,     0.3),
    (8, 0,     23.6,      0.0,    -0.1,     0.0),
    (8, 1,      9.8,      8.4,     0.1,    -0.3),
    (8, 2,    -17.5,    -15.3,    -0.1,     0.7),
    (8, 3,     -0.4,     12.8,     0.5,     0.2),
    (8, 4,    -21.1,    -11.8,    -0.1,     0.7),
    (8, 5,     15.3,     14.9,     0.4,    -0.2),
    (8, 6,     13.7,      3.6,     0.5,    -0.2),
    (8, 7,    -16.5,     -6.9,     0.0,     0.3),
    (8, 8,     -0.3,      2.8,     0.4,     0.1),
    (9, 0,      5.0,      0.0,    -0.1,     0.0),
    (9, 1,      8.2,    -23.3,    -0.2,    -0.3),
    (9, 2,      2.9,     11.1,    -0.1,     0.2),
    (9, 3,     -1.4,      9.8,     0.2,    -0.1),
    (9, 4,     -1.1,     -5.1,    -0.2,     0.4),
    (9, 5,    -13.3,     -6.2,     0.0,     0.1),
    (9, 6,      1.1,      7.8,     0.1,    -0.1),
    (9, 7,      8.9,      0.4,    -0.1,    -0.2),
    (9, 8,     -9.3,     -4.1,    -0.1,     0.0),
    (9, 9,    -10.5,      8.5,    -0.1,     0.2),
    (10, 0,    -1.9,      0.0,     0.0,     0.0),
    (10, 1,    -6.5,      3.3,     0.0,     0.1),
    (10, 2,     0.2,     -0.3,    -0.1,    -0.1),
    (10, 3,     0.6,      4.6,     0.0,     0.0),
    (10, 4,    -0.6,      4.4,     0.0,    -0.1),
    (10, 5,     1.7,     -7.9,    -0.1,    -0.2),
    (10, 6,    -0.7,     -0.6,    -0.2,     0.1),
    (10, 7,     2.1,     -4.1,     0.0,    -0.1),
    (10, 8,     2.3,     -2.8,    -0.2,    -0.2),
    (10, 9,    -1.8,     -1.1,    -0.1,     0.1),
    (10,10,    -3.6,     -8.7,     0.0,    -0.2),
    (11, 0,     3.1,      0.0,     0.0,     0.0),
    (11, 1,    -1.5,     -0.1,     0.0,     0.0),
    (11, 2,    -2.3,      2.1,     0.0,     0.1),
    (11, 3,     2.1,     -0.7,     0.1,     0.0),
    (11, 4,    -0.9,     -1.1,     0.0,     0.1),
    (11, 5,     0.6,      0.7,     0.0,     0.0),
    (11, 6,    -0.7,     -0.2,     0.0,     0.0),
    (11, 7,     0.2,     -2.1,     0.0,     0.1),
    (11, 8,     1.7,     -1.5,     0.0,     0.0),
    (11, 9,    -0.2,     -2.5,     0.0,    -0.1),
    (11,10,     0.4,     -2.0,    -0.1,     0.0),
    (11,11,     3.5,     -2.3,    -0.1,    -0.1),
    (12, 0,    -2.0,      0.0,     0.0,     0.0),
    (12, 1,    -0.3,     -1.0,     0.0,     0.0),
    (12, 2,     0.4,      0.5,     0.0,     0.0),
    (12, 3,     1.3,      1.8,     0.1,    -0.1),
    (12, 4,    -0.9,     -2.2,    -0.1,     0.0),
    (12, 5,     0.9,      0.3,     0.0,     0.0),
    (12, 6,     0.1,      0.7,     0.1,     0.0),
    (12, 7,     0.5,     -0.1,     0.0,     0.0),
    (12, 8,    -0.4,      0.3,     0.0,     0.0),
    (12, 9,    -0.4,      0.2,     0.0,     0.0),
    (12,10,     0.2,     -0.9,     0.0,     0.0),
    (12,11,    -0.9,     -0.2,     0.0,     0.0),
    (12,12,     0.0,      0.7,     0.0,     0.0),
]

# ─────────────────────────────────────────────────────────────────────────────
# WMM spherical harmonic computation
# ─────────────────────────────────────────────────────────────────────────────

WGS84_A  = 6378.137       # semi-major axis (km)
WGS84_F  = 1.0 / 298.257223563
WGS84_B  = WGS84_A * (1.0 - WGS84_F)  # semi-minor axis
RE       = 6371.2          # WMM reference radius (km)

NMAX = 12  # max degree


def load_coefficients_from_file(path):
    """Load WMM.COF file and return (epoch, coefficients_list)."""
    coeffs = []
    epoch = None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('9' * 5):
                continue
            parts = line.split()
            if epoch is None:
                # First line: epoch and model name
                epoch = float(parts[0])
                continue
            n, m = int(parts[0]), int(parts[1])
            if n == 0 and m == 0:
                continue
            gnm  = float(parts[2])
            hnm  = float(parts[3])
            dgnm = float(parts[4])
            dhnm = float(parts[5])
            coeffs.append((n, m, gnm, hnm, dgnm, dhnm))
    return epoch, coeffs


def compute_declination(lat_deg, lon_deg, alt_km, year, epoch, coeffs):
    """Compute magnetic declination using WMM spherical harmonics."""

    dt = year - epoch
    lat_rad = math.radians(lat_deg)
    lon_rad = math.radians(lon_deg)
    sin_lat = math.sin(lat_rad)
    cos_lat = math.cos(lat_rad)

    # Geodetic to geocentric conversion
    a2 = WGS84_A * WGS84_A
    b2 = WGS84_B * WGS84_B
    D2 = a2 * cos_lat * cos_lat + b2 * sin_lat * sin_lat
    D  = math.sqrt(D2)
    Rg = math.sqrt((a2 * a2 * cos_lat * cos_lat + b2 * b2 * sin_lat * sin_lat) / D2)
    Rg = Rg + alt_km

    sin_lat_gc = (b2 / a2) * sin_lat / math.sqrt((b2 / a2) * (b2 / a2) * sin_lat * sin_lat + cos_lat * cos_lat)
    cos_lat_gc = math.sqrt(1.0 - sin_lat_gc * sin_lat_gc)
    if cos_lat >= 0:
        cos_lat_gc = abs(cos_lat_gc)
    else:
        cos_lat_gc = -abs(cos_lat_gc)

    lat_gc_rad = math.asin(sin_lat_gc)

    # Ratio
    ratio = RE / Rg

    # Build associated Legendre polynomials (Schmidt semi-normalized)
    P = [[0.0] * (NMAX + 2) for _ in range(NMAX + 2)]
    dP = [[0.0] * (NMAX + 2) for _ in range(NMAX + 2)]

    P[0][0] = 1.0
    P[1][0] = sin_lat_gc
    P[1][1] = cos_lat_gc
    dP[0][0] = 0.0
    dP[1][0] = cos_lat_gc
    dP[1][1] = -sin_lat_gc

    for n in range(2, NMAX + 1):
        for m in range(0, n + 1):
            if m == n:
                P[n][m] = cos_lat_gc * P[n-1][m-1]
                dP[n][m] = cos_lat_gc * dP[n-1][m-1] - sin_lat_gc * P[n-1][m-1]
            elif m == n - 1:
                P[n][m] = sin_lat_gc * P[n-1][m]
                dP[n][m] = sin_lat_gc * dP[n-1][m] + cos_lat_gc * P[n-1][m]
            else:
                K = ((n - 1.0) * (n - 1.0) - m * m) / ((2.0 * n - 1.0) * (2.0 * n - 3.0))
                P[n][m] = sin_lat_gc * P[n-1][m] - K * P[n-2][m]
                dP[n][m] = sin_lat_gc * dP[n-1][m] + cos_lat_gc * P[n-1][m] - K * dP[n-2][m]

    # Schmidt normalization factors
    S = [[0.0] * (NMAX + 2) for _ in range(NMAX + 2)]
    S[0][0] = 1.0
    for n in range(1, NMAX + 1):
        S[n][0] = S[n-1][0] * (2.0 * n - 1.0) / n
        for m in range(1, n + 1):
            if m == 1:
                S[n][m] = S[n][m-1] * math.sqrt(2.0 / (n * (n + 1.0)))
            else:
                S[n][m] = S[n][m-1] * math.sqrt((n - m + 1.0) / (n + m))

    # Sum field components in geocentric frame
    Br = 0.0   # radial (outward)
    Bt = 0.0   # theta (southward)
    Bp = 0.0   # phi (eastward)

    for (n, m, gnm, hnm, dgnm, dhnm) in coeffs:
        if n > NMAX:
            continue
        g = gnm + dt * dgnm
        h = hnm + dt * dhnm
        rr = ratio ** (n + 2)
        cos_m = math.cos(m * lon_rad)
        sin_m = math.sin(m * lon_rad)

        Pnm = P[n][m] * S[n][m]
        dPnm = dP[n][m] * S[n][m]

        Br += (n + 1) * rr * (g * cos_m + h * sin_m) * Pnm
        Bt -= rr * (g * cos_m + h * sin_m) * dPnm
        if cos_lat_gc != 0:
            Bp -= rr * m * (-g * sin_m + h * cos_m) * Pnm / cos_lat_gc

    # Rotate from geocentric to geodetic
    psi = lat_rad - lat_gc_rad
    Bx = Bt * math.cos(psi) - Br * math.sin(psi)  # north
    By = Bp                                          # east
    # Bz = Bt * math.sin(psi) + Br * math.cos(psi)  # down (unused)

    # Declination
    decl = math.degrees(math.atan2(By, Bx))
    return decl


def main():
    parser = argparse.ArgumentParser(description="Generate WMM magnetic declination grid CSV")
    parser.add_argument("--year", type=float, default=None,
                        help="Decimal year (default: epoch + 2.5)")
    parser.add_argument("--step", type=float, default=0.5,
                        help="Grid step in degrees (default: 0.5)")
    parser.add_argument("--alt", type=float, default=0.0,
                        help="Altitude in km MSL (default: 0 = sea level)")
    parser.add_argument("--cof", type=str, default=None,
                        help="Path to WMM.COF coefficient file (overrides built-in)")
    parser.add_argument("--out", type=str, default="Data/magdec.csv",
                        help="Output CSV path (default: Data/magdec.csv)")
    args = parser.parse_args()

    # Load coefficients
    if args.cof:
        epoch, coeffs = load_coefficients_from_file(args.cof)
        print(f"Loaded coefficients from {args.cof}, epoch {epoch}")
    else:
        epoch = EPOCH
        coeffs = WMM_COEFFICIENTS
        print(f"Using built-in WMM2020 coefficients, epoch {epoch}")

    year = args.year if args.year else epoch + 2.5
    step = args.step
    alt = args.alt

    print(f"Year: {year}, step: {step}°, altitude: {alt} km")

    # Generate grid (north to south, west to east — matches existing format)
    lat_values = []
    lat = 90.0
    while lat >= -90.0 - step / 2:
        lat_values.append(round(lat, 1))
        lat -= step
    lon_values = []
    lon = -180.0
    while lon <= 180.0 + step / 2:
        lon_values.append(round(lon, 1))
        lon += step

    total = len(lat_values) * len(lon_values)
    print(f"Grid: {len(lat_values)} x {len(lon_values)} = {total} points")

    os.makedirs(os.path.dirname(args.out) if os.path.dirname(args.out) else ".", exist_ok=True)

    count = 0
    with open(args.out, "w") as f:
        for lat in lat_values:
            for lon in lon_values:
                # Clamp poles slightly to avoid singularity
                clat = max(-89.999, min(89.999, lat))
                dec = compute_declination(clat, lon, alt, year, epoch, coeffs)
                f.write(f"{lat},{lon},{dec:.1f}\n")
                count += 1
                if count % 10000 == 0:
                    pct = 100.0 * count / total
                    print(f"  {count}/{total} ({pct:.0f}%)")

    print(f"Done. Wrote {count} points to {args.out}")
    print(f"Model: WMM epoch {epoch}, computed at {year}")


if __name__ == "__main__":
    main()
