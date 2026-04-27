#ifndef SIM_INTERFACES__WEATHER_CONVENTION_HPP_
#define SIM_INTERFACES__WEATHER_CONVENTION_HPP_

namespace sim_interfaces {

// Framework convention: the patch transition perimeter (the band over which
// patch influence ramps from 0 to 1) is a fixed fraction of the patch radius,
// applied INWARD from the authored radius. The authored radius is the hard
// outer boundary of all patch effect — nothing patch-related leaks past it.
//
// Influence math (used by both the FDM-side weather_solver and the X-Plane
// plugin's patch_influence helper, and emitted on the wire as
// transition_perimeter_m in the CIGI Environmental Region Control packet):
//
//   tp    = radius * PATCH_TRANSITION_PERIMETER_FRACTION
//   inner = radius - tp
//   d <= inner   → w = 1.0
//   d <  radius  → w = (radius - d) / tp     (linear ramp 1 → 0)
//   d >= radius  → w = 0.0
//
// Defensive: if tp >= radius (small patch, fraction would consume the whole
// disk), each consumer falls back to a hard switch at radius — no ramp.
//
// The FDM solver, the host-side CIGI emitter (weather_sync.cpp), and the
// X-Plane plugin all reference this single symbol so the geographic band
// where wind/temperature/pressure (FDM side) and visibility/clouds (visual
// side) blend stays aligned. Edit the value once here to change both sides.
constexpr double PATCH_TRANSITION_PERIMETER_FRACTION = 0.25;

}  // namespace sim_interfaces

#endif  // SIM_INTERFACES__WEATHER_CONVENTION_HPP_
