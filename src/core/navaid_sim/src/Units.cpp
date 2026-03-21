#include "Units.h"

using namespace std;

const double nm_to_m_factor = 1853.249f;
const double nm_to_rad_factor = 0.000290888208f; // assumes spherical earth
const double deg_to_min_factor = 60.0f;
const double min_to_sec_factor = 60.0f;
const double deg_to_sec_factor = (60.0f * 60.0f);
const double deg_to_rad_factor = pi / 180.0f;
const double slug_to_kg_factor = 14.59f;
const double m_to_ft_factor = 3.281f;
const double ft_per_sec_to_knots_factor = 0.592f;
const double m_per_sec_to_km_per_h_factor = 3.6f;
const double ft_to_nm_factor = 1.0 / 6076.12;

// angle

double deg_to_rad(const double angle)
{
	return (angle * deg_to_rad_factor);
}

double rad_to_deg(const double angle)
{
	return (angle / deg_to_rad_factor);
}

double deg_to_min(const double degrees)
{
	return (degrees * deg_to_min_factor);
}

double min_to_deg(const double minutes)
{
	return (minutes / deg_to_min_factor);
}

double deg_to_sec(const double degrees)
{
	return (degrees * deg_to_sec_factor);
}

double sec_to_deg(const double seconds)
{	
	return (seconds / deg_to_sec_factor);
}

double min_to_sec(const double minutes)
{
	return (minutes * min_to_sec_factor);
}

double sec_to_min(const double seconds)
{
	return (seconds / min_to_sec_factor);
}

int get_deg_part(const double degrees)
{
	int result = (int)(floor(fabs(degrees)));
	if(result < 0)
		result = -result;
	return result;
}

int get_min_part(const double degrees)
{
	double total_minutes = deg_to_min(fabs(degrees));
	double sub_deg_minutes = fmod(total_minutes,deg_to_min_factor);
	int result = (int)floor(sub_deg_minutes);
	if(degrees < 0)
		result = -result;
	return result;
}

double get_sec_part(const double degrees)
{
	double result = fmod(min_to_sec(deg_to_min(fabs(degrees))),min_to_sec_factor);
	if (degrees < 0)
		result = -result;
	return result;
}

// distance

double m_to_nm(const double distance_m)
{
	return (distance_m / nm_to_m_factor);
}

double nm_to_m(const double distance_nm)
{
	return (distance_nm * nm_to_m_factor);
}

double nm_to_rad(const double distance_nm)
{
	return (distance_nm * nm_to_rad_factor);
}

double rad_to_nm(const double distance_rad)
{
	return (distance_rad / nm_to_rad_factor);
}

double m_to_ft(const double distance_m)
{
	return (distance_m * m_to_ft_factor);
}

double ft_to_m(const double distance_ft)
{
	return (distance_ft / m_to_ft_factor);
}

double ft_to_nm(const double distance_ft)
{
	return (distance_ft * ft_to_nm_factor);
}

// speed
double knots_to_ft_per_sec(const double speed_knots)
{
	return (speed_knots / ft_per_sec_to_knots_factor);
}

double ft_per_sec_to_knots(const double speed_ft_per_sec)
{
	return (speed_ft_per_sec * ft_per_sec_to_knots_factor);
}

double knots_to_m_per_sec(const double speed_knots)
{
	return (ft_to_m(knots_to_ft_per_sec(speed_knots)));
}

double m_per_sec_to_knots(const double speed_m_per_sec)
{
	return (ft_per_sec_to_knots(m_to_ft(speed_m_per_sec)));
}

double km_per_h_to_m_per_sec(const double speed_km_per_h)
{
	return (speed_km_per_h / m_per_sec_to_km_per_h_factor);
}

double m_per_sec_to_km_per_h(const double speed_m_per_sec)
{
	return (speed_m_per_sec * m_per_sec_to_km_per_h_factor);
}


// temperature

double celcius_to_fahrenheit(const double temperature_celcius)
{
	return ((9.0f/5.0f) * temperature_celcius + 32.0f);
}

double fahrenheit_to_celcius(const double temperature_fahrenheit)
{
	return ((5.0f/9.0f) * (temperature_fahrenheit - 32.0f));
}

double celcius_to_kelvin(const double temperature_celcius)
{
	return (temperature_celcius + 273.15f);
}

double kelvin_to_celcius(const double temperature_kelvin)
{
	return (temperature_kelvin - 273.15f);
}

double fahrenheit_to_kelvin(const double temperature_fahrenheit)
{
	return celcius_to_kelvin(fahrenheit_to_celcius(temperature_fahrenheit));
}
double kelvin_to_fahrenheit(const double temperature_kelvin)
{
	return celcius_to_fahrenheit(kelvin_to_celcius(temperature_kelvin));
}

// weight

double slug_to_kg(const double weight_slug)
{
	return weight_slug * slug_to_kg_factor;
}

double kg_to_slug(const double weight_kg)
{
	return weight_kg / slug_to_kg_factor;
}

// density

double slug_per_cube_ft_to_kg_per_cube_m(const double slug_per_cube_ft)
{
	double kg_per_cube_ft = slug_to_kg(slug_per_cube_ft);
	double kg_per_cube_m = kg_per_cube_ft / pow(ft_to_m(1.0f),3);
	return kg_per_cube_m;
}

double kg_per_cube_m_to_slug_per_cube_ft(const double kg_per_cube_m)
{
	double slug_per_cube_m = kg_to_slug(kg_per_cube_m);
	double slug_per_cube_ft = slug_per_cube_m / pow(m_to_ft(1.0f),3);
	return slug_per_cube_ft;
}
