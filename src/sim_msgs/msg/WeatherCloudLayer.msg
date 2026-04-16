# WeatherCloudLayer.msg — sub-message for cloud layers in WeatherState
# Maps to CIGI 3.3 Weather Control packet (0x0C) cloud fields

uint8   cloud_type               # CIGI enum: 0=None, 1=Altocumulus, 2=Altostratus,
                                 # 3=Cirrocumulus, 4=Cirrostratus, 5=Cirrus,
                                 # 6=Cumulonimbus, 7=Cumulus, 8=Nimbostratus,
                                 # 9=Stratocumulus, 10=Stratus, 11-15=Other
float32 coverage_pct             # 0-100 (CIGI convention, maps to XP 0-1 via /100)
float32 base_elevation_m         # altitude of layer base, MSL (m)
float32 thickness_m              # vertical extent (m), top = base + thickness
float32 transition_band_m        # CIGI transition band above/below layer (m)
bool    scud_enable              # scud effects in transition bands
float32 scud_frequency_pct       # 0-100, density of scud effect
