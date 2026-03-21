#pragma once
#ifndef UNITS_H
#define UNITS_H

#include <math.h>
#include <iostream>

// constants
const double pi = 3.141592653589793238462643383280f;
const double g = -9.82f;
const double r = 287.05f;
const double isa_base_pressure_pa = 101300.0f;
const double isa_base_density = 1.225f;
const double isa_base_temperature_c = 15.2f;
const double earth_radius_nm = 3440.069; // Earth's radius in nautical miles

// angle
double deg_to_rad(const double angle);
double rad_to_deg(const double angle);
double deg_to_sec(const double degrees);
double sec_to_deg(const double seconds);
double deg_to_min(const double degrees);
double min_to_deg(const double minutes);
double min_to_sec(const double minutes);
double sec_to_min(const double seconds);
int get_deg_part(const double degrees);
int get_min_part(const double degrees);
double get_sec_part(const double degrees);

// distance
double m_to_nm(const double distance_m);
double nm_to_m(const double distance_nm);
double nm_to_rad(const double distance_nm);  // spherical earth
double rad_to_nm(const double distance_rad); // spherical earth
double m_to_ft(const double distance_m);
double ft_to_m(const double distance_ft);
double ft_to_nm(const double distance_ft);

// speed
double knots_to_ft_per_sec(const double speed_knots);
double ft_per_sec_to_knots(const double speed_ft_per_sec);
double knots_to_m_per_sec(const double speed_knots);
double m_per_sec_to_knots(const double speed_m_per_sec);
double km_per_h_to_m_per_sec(const double speed_km_per_h);
double m_per_sec_to_km_per_h(const double speed_m_per_sec);

// temperature
double celcius_to_fahrenheit(const double temperature_celcius);
double fahrenheit_to_celcius(const double temperature_fahrenheit);
double celcius_to_kelvin(const double temperature_celcius);
double kelvin_to_celcius(const double temperature_kelvin);
double fahrenheit_to_kelvin(const double temperature_fahrenheit);
double kelvin_to_fahrenheit(const double temperature_kelvin);

// weight
double slug_to_kg(const double weight_slug);
double kg_to_slug(const double weight_kg);

// density
double slug_per_cube_ft_to_kg_per_cube_m(const double slug_per_cube_ft);
double kg_per_cube_m_to_slug_per_cube_ft(const double kg_per_cube_m);

#endif
