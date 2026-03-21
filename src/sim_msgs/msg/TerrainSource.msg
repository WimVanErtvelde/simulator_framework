# Terrain source currently active
# Published by: flight_model_adapter -> /sim/terrain/source at 1Hz
uint8 source
uint8 SOURCE_CIGI=0      # IG HOT responses (green — matches visual)
uint8 SOURCE_SRTM=1      # SRTM fallback (yellow — reduced resolution)
uint8 SOURCE_MSL=2       # 0ft MSL fallback (red — no terrain data)
uint8 SOURCE_UNKNOWN=255

string description       # human readable e.g. "CIGI HOT (3 points)"
