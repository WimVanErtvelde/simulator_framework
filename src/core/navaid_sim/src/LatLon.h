#pragma once
#ifndef LATLONG_H
#define LATLONG_H

#include <iostream>
#include <math.h>
#include "Units.h"

// Latitude: North positive, South negative
// Longitude: East positive, West negative

class LatLon {
public:
	LatLon();
	LatLon(const double lat_deg,const double lon_deg);
	LatLon(const double lat_deg,const double lat_min, const double lat_sec,const double lon_deg,const double lon_min,const double lon_sec);
	
	double get_lat_deg() const;
	double get_lon_deg() const;
	double get_lat_rad() const;
	double get_lon_rad() const;

	void set_lat_deg(const double lat_deg);
	void set_lon_deg(const double lon_deg);
	void set_lat_rad(const double lat_rad);
	void set_lon_rad(const double lon_rad);

	float get_distance_rad(LatLon other_latlon) const;
	float get_distance_nm(LatLon other_latlon) const;
	float get_course_rad(LatLon other_latlon) const;
	float get_course_deg(LatLon other_latlon) const;

	static float calculateSlantRangeNM(const LatLon& p1, float height1_ft, const LatLon& p2, float height2_ft);
private:
	double lat; // rad
	double lon; // rad
};

std::ostream& operator<<(std::ostream& os, const LatLon& latlon);

#endif