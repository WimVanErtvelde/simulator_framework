#include "LatLon.h"

using namespace std;

LatLon::LatLon(){}

LatLon::LatLon(const double lat_deg,const double lon_deg)
{
	lat = deg_to_rad(lat_deg);
	lon = deg_to_rad(lon_deg);
}

LatLon::LatLon(const double lat_deg,const double lat_min,const double lat_sec,const double lon_deg,const double lon_min,const double lon_sec)
{
	lat = deg_to_rad(lat_deg + min_to_deg(lat_min) + sec_to_deg(lat_sec));
	lon = deg_to_rad(lon_deg + min_to_deg(lon_min) + sec_to_deg(lon_sec));
}

double LatLon::get_lat_deg() const
{
	return rad_to_deg(lat);
}

double LatLon::get_lon_deg() const
{
	return rad_to_deg(lon);
}

double LatLon::get_lat_rad() const
{
	return lat;
}

double LatLon::get_lon_rad() const
{
	return lon;
}

void LatLon::set_lat_deg(const double lat_deg)
{
	lat = deg_to_rad(lat_deg);
}

void LatLon::set_lon_deg(const double lon_deg)
{
	lon = deg_to_rad(lon_deg);

}

void LatLon::set_lat_rad(const double lat_rad)
{
	lat = lat_rad;
}

void LatLon::set_lon_rad(const double lon_rad)
{
	lon = lon_rad;
}


float LatLon::get_distance_rad(LatLon  other) const
{
	double lat1 = get_lat_rad();
	double lon1 = get_lon_rad();
	double lat2 = other.get_lat_rad();
	double lon2 = other.get_lon_rad();

	return  (float)(2*asin(sqrt(pow((sin((lat1-lat2)/2)),2) + cos(lat1)*cos(lat2)*pow((sin((lon1-lon2)/2)),2))));
}

float LatLon::get_distance_nm(LatLon other) const
{
	return (float)get_distance_rad(other) * 3440.0f;
}

float LatLon::get_course_rad(LatLon other) const
{
	double lat1 = get_lat_rad();
	double lon1 = get_lon_rad();
	double lat2 = other.get_lat_rad();
	double lon2 = other.get_lon_rad();

	double dlon = lon2 - lon1;
	double course = atan2(sin(dlon) * cos(lat2),cos(lat1)*sin(lat2) - sin(lat1) * cos(lat2) * cos(dlon));
	return (float)course;
}

float LatLon::get_course_deg(LatLon other) const
{
	return (float)rad_to_deg(get_course_rad(other));
}

float LatLon::calculateSlantRangeNM(const LatLon& p1, float height1_ft, const LatLon& p2, float height2_ft)
{
	// Convert degrees to radians
	float lat1_rad = static_cast<float>(p1.lat);
	float lon1_rad = static_cast<float>(p1.lon);
	float lat2_rad = static_cast<float>(p2.lat);
	float lon2_rad = static_cast<float>(p2.lon);

	// Convert heights from feet to NM
	float h1_nm = static_cast<float>(m_to_nm(height1_ft));
	float h2_nm = static_cast<float>(m_to_nm(height2_ft));

	// Total radius at each point (Earth's radius + height)
	float r1 = static_cast<float>(earth_radius_nm) + h1_nm;
	float r2 = static_cast<float>(earth_radius_nm) + h2_nm;

	// Convert to Cartesian coordinates (x, y, z)
	float x1 = r1 * std::cos(lat1_rad) * std::cos(lon1_rad);
	float y1 = r1 * std::cos(lat1_rad) * std::sin(lon1_rad);
	float z1 = r1 * std::sin(lat1_rad);

	float x2 = r2 * std::cos(lat2_rad) * std::cos(lon2_rad);
	float y2 = r2 * std::cos(lat2_rad) * std::sin(lon2_rad);
	float z2 = r2 * std::sin(lat2_rad);

	// Calculate 3D Euclidean distance
	float dx = x2 - x1;
	float dy = y2 - y1;
	float dz = z2 - z1;

	float slantRange = std::sqrt(dx * dx + dy * dy + dz * dz);
	return slantRange;
}

ostream& operator<<(ostream& os, const LatLon& latlon)
{
	const double lat_deg = latlon.get_lat_deg();
	const double lon_deg = latlon.get_lon_deg();
	os << "(" << lat_deg << "," << lon_deg << ")";
	//os << get_deg_part(lat_deg) << "deg " << get_min_part(lat_deg) << "min " << get_sec_part(lat_deg) << "sec N ";
	//os << get_deg_part(lon_deg) << "deg " << get_min_part(lon_deg) << "min " << get_sec_part(lon_deg) << "sec E ";
	return os;
}
