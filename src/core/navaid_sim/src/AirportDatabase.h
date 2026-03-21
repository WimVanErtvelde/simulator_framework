#pragma once
#ifndef AIRPORT_DATABASE_H
#define AIRPORT_DATABASE_H

#include <string>
#include <vector>
#include <unordered_map>

struct RunwayEnd {
    std::string designator;       // "25L", "07R", "01", etc
    double threshold_lat_rad = 0;
    double threshold_lon_rad = 0;
    double displaced_threshold_m = 0;
    double heading_deg = 0;       // magnetic
    double elevation_m = 0;
};

struct Runway {
    RunwayEnd end1;
    RunwayEnd end2;
    double width_m = 0;
    int surface_type = 0;         // XP convention: 1=asphalt, 2=concrete, etc
    double length_m = 0;
};

struct Airport {
    std::string icao;
    std::string name;
    std::string city;
    std::string country;
    std::string iata;
    double arp_lat_rad = 0;       // Airport Reference Point
    double arp_lon_rad = 0;
    double elevation_m = 0;
    double transition_altitude_ft = 0;
    std::vector<Runway> runways;
};

class AirportDatabase
{
public:
    // Load from ARINC-424 file (euramec.pc) — PA and PG subsection records
    bool loadA424(const std::string & path);

    // Load from X-Plane apt.dat (XP1200 format) — row codes 1, 1302, 100
    bool loadXP12(const std::string & path);

    // Lookup by exact ICAO
    const Airport * findByICAO(const std::string & icao) const;

    // Search: case-insensitive ICAO prefix match, then partial name match
    std::vector<const Airport *> search(const std::string & query, size_t max_results = 10) const;

    size_t airportCount() const { return airports_.size(); }
    size_t runwayCount() const;

private:
    std::unordered_map<std::string, Airport> airports_;  // keyed by ICAO

    // A424 helpers
    static void parseA424LatLon(const char * data, double & lat_rad, double & lon_rad);
    static std::string trimmed(const char * buf, size_t len);
    static int parseInt(const char * buf, size_t len);
    static double parseDbl(const char * buf, size_t len);

    // XP12 helpers
    static double xpLatLonToRad(double deg);
};

#endif // AIRPORT_DATABASE_H
