# WeatherState.msg
# Published by: ios_backend → /world/weather
# Event-driven: on IOS Accept, preset load, or scenario load
# Consumed by: weather_solver_node, cigi_bridge, SimSnapshot save
#
# This is the instructor-authored current weather state. NOT a forecast.
# weather_solver_node interpolates this at aircraft position → AtmosphereState.
# cigi_bridge encodes this into CIGI 3.3 packets for the IG.

std_msgs/Header header

# ── Global atmosphere (maps to CIGI 0x0A Atmosphere Control) ──────────────
float64 temperature_sl_k         # sea-level temperature (K)
float64 pressure_sl_pa           # sea-level pressure (Pa), ISA = 101325
float64 visibility_m             # horizontal visibility (m)
uint8   humidity_pct             # global relative humidity (0-100 %)

# ── Cloud layers (maps to CIGI 0x0C, layer_id 1-3) ───────────────────────
# Dynamic array, max 3 layers (X-Plane limit). Empty = clear skies.
sim_msgs/WeatherCloudLayer[] cloud_layers

# ── Wind layers (maps to CIGI 0x0C, IG-defined layer_ids 10+) ────────────
# Dynamic array, max 13 layers (X-Plane limit). Empty = calm.
sim_msgs/WeatherWindLayer[] wind_layers

# ── Precipitation ─────────────────────────────────────────────────────────
# Maps to CIGI 0x0C layer_id 4=Rain, 5=Snow
float32 precipitation_rate       # 0.0-1.0 intensity ratio
uint8   precipitation_type       # 0=None, 1=Rain, 2=Snow, 3=Sleet

# ── Surface conditions ────────────────────────────────────────────────────
# Maritime (maps to CIGI 0x0D/0x0E Maritime Surface Conditions)
float32 wave_height_m            # wave amplitude (m)
float32 wave_direction_deg       # direction waves come from (deg true)
# Terrestrial (maps to CIGI 0x0F Terrestrial Surface Conditions)
uint8   runway_friction          # 0-15 enum: Dry=0, wet 1-3, puddly 4-6,
                                 # snowy 7-9, icy 10-12, snowy/icy 13-15

# ── FSTD control (maps to CIGI user-defined 0xCB) ────────────────────────
float32 variability_pct          # 0.0-1.0, XP weather variability (0 for QTG determinism)
uint8   evolution_mode           # 0-7 XP change_mode enum (3=Static for QTG)
uint32  deterministic_seed       # RNG seed for Dryden turbulence (0 = random)

# ── Turbulence model selection (maps to CIGI user-defined 0xC9) ──────────
uint8   turbulence_model         # 0=None, 1=MIL-F-8785C, 2=MIL-HDBK-1797A, 3=ESDU-85020

# ── Weather station reference ─────────────────────────────────────────────
# All weather is authored relative to a station airport.
# Cloud heights are authored in ft AGL and converted to m MSL using station elevation.
string  station_icao              # ICAO code (e.g., "EBBR"), empty = not set
float64 station_elevation_m       # station elevation MSL (m), from airport DB

# ── Localized weather patches (maps to CIGI 0x0B + 0x0C Scope=Regional) ──
# Each patch = one Environmental Region + N Weather Control Scope=Regional
# packets. Empty array = global weather only. See WeatherPatch.msg.
sim_msgs/WeatherPatch[] patches

# ── Microburst hazards (maps to CIGI user-defined 0xCA) ──────────────────
# Dynamic array. Empty = no microbursts.
sim_msgs/MicroburstHazard[] microbursts
