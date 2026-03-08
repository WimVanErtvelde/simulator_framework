# Flight dynamics model state output
builtin_interfaces/Time stamp

# Position
float64 latitude_deg
float64 longitude_deg
float64 altitude_m

# Attitude (radians)
float64 phi
float64 theta
float64 psi

# Body velocities (m/s)
float64 u
float64 v
float64 w

# Body angular rates (rad/s)
float64 p
float64 q
float64 r

# Accelerations (m/s^2)
float64 ax
float64 ay
float64 az

# Aero forces (N)
float64 lift
float64 drag
float64 side_force

# Airdata
float64 indicated_airspeed_kts
float64 true_airspeed_kts
float64 groundspeed_kts
float64 mach
float64 alpha_deg
float64 beta_deg

# Gear
bool weight_on_wheels
