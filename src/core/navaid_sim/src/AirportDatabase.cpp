#include "AirportDatabase.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cctype>

static constexpr double PI = 3.14159265358979323846;
static constexpr double DEG2RAD = PI / 180.0;
static constexpr double FT2M = 0.3048;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string AirportDatabase::trimmed(const char * buf, size_t len)
{
    std::string s(buf, len);
    // trim trailing spaces
    size_t end = s.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return "";
    s.erase(end + 1);
    // trim leading spaces
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    return s.substr(start);
}

int AirportDatabase::parseInt(const char * buf, size_t len)
{
    char tmp[16] = {};
    size_t n = std::min(len, sizeof(tmp) - 1);
    std::memcpy(tmp, buf, n);
    return std::atoi(tmp);
}

double AirportDatabase::parseDbl(const char * buf, size_t len)
{
    char tmp[32] = {};
    size_t n = std::min(len, sizeof(tmp) - 1);
    std::memcpy(tmp, buf, n);
    return std::atof(tmp);
}

// A424 lat/lon format: 9-char lat + 10-char lon concatenated (19 chars)
// Lat: N/SDDMMSS.S (chars 0-8)   Lon: E/WDDDMMSS.S (chars 9-18)
void AirportDatabase::parseA424LatLon(const char * data, double & lat_rad, double & lon_rad)
{
    // Latitude (9 chars): [0]=N/S [1-2]=DD [3-4]=MM [5-8]=SS.S (×100)
    int lat_sign = (data[0] == 'S') ? -1 : 1;
    double lat_deg = (data[1] - '0') * 10.0 + (data[2] - '0');
    double lat_min = (data[3] - '0') * 10.0 + (data[4] - '0');
    double lat_sec = ((data[5] - '0') * 1000 + (data[6] - '0') * 100 +
                      (data[7] - '0') * 10 + (data[8] - '0')) / 100.0;
    lat_rad = lat_sign * (lat_deg + lat_min / 60.0 + lat_sec / 3600.0) * DEG2RAD;

    // Longitude (10 chars): [9]=E/W [10-12]=DDD [13-14]=MM [15-18]=SS.S (×100)
    int lon_sign = (data[9] == 'W') ? -1 : 1;
    double lon_deg = (data[10] - '0') * 100.0 + (data[11] - '0') * 10.0 + (data[12] - '0');
    double lon_min = (data[13] - '0') * 10.0 + (data[14] - '0');
    double lon_sec = ((data[15] - '0') * 1000 + (data[16] - '0') * 100 +
                      (data[17] - '0') * 10 + (data[18] - '0')) / 100.0;
    lon_rad = lon_sign * (lon_deg + lon_min / 60.0 + lon_sec / 3600.0) * DEG2RAD;
}

double AirportDatabase::xpLatLonToRad(double deg)
{
    return deg * DEG2RAD;
}

static std::string toUpper(const std::string & s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// A424 loader (euramec.pc)
// ─────────────────────────────────────────────────────────────────────────────

bool AirportDatabase::loadA424(const std::string & path)
{
    FILE * f = fopen(path.c_str(), "r");
    if (!f) return false;

    char line[150];
    while (fgets(line, sizeof(line), f)) {
        size_t len = std::strlen(line);
        if (len < 40) continue;

        // Airport section: col 5='P', col 6=' '
        if (line[4] != 'P' || line[5] != ' ') continue;

        char subsection = line[12];
        char continuation = line[21];  // continuation record number

        // Only process primary records (continuation '1' or '0' or 'A'-level)
        if (subsection == 'A' && continuation == '1') {
            // Airport Reference record
            std::string icao = trimmed(&line[6], 4);
            if (icao.empty()) continue;

            Airport & apt = airports_[icao];
            apt.icao = icao;
            apt.name = trimmed(&line[93], 30);  // cols 94-123

            // ARP lat/lon: cols 33-51 (19 chars: 9 lat + 10 lon)
            if (line[32] == 'N' || line[32] == 'S') {
                parseA424LatLon(&line[32], apt.arp_lat_rad, apt.arp_lon_rad);
            }

            // Elevation: cols 57-61 (5 chars, in feet)
            apt.elevation_m = parseInt(&line[56], 5) * FT2M;

            // Transition altitude from field 79-83 (5 chars) if present
            // Format varies; try to extract
            if (len > 83) {
                int ta = parseInt(&line[78], 5);
                if (ta > 0) apt.transition_altitude_ft = ta;
            }
        }
        else if (subsection == 'G' && continuation == '1') {
            // Runway record
            std::string icao = trimmed(&line[6], 4);
            if (icao.empty()) continue;

            auto it = airports_.find(icao);
            if (it == airports_.end()) continue;  // airport not yet seen

            Runway rwy;

            // Runway ID: cols 14-18 (5 chars, e.g. "RW25L")
            std::string rwy_id = trimmed(&line[13], 5);
            // Strip leading "RW"
            if (rwy_id.size() >= 2 && rwy_id[0] == 'R' && rwy_id[1] == 'W')
                rwy_id = rwy_id.substr(2);
            rwy.end1.designator = rwy_id;

            // Length: cols 23-27 (pos 22-26, 5 chars, feet)
            rwy.length_m = parseInt(&line[22], 5) * FT2M;

            // Heading: cols 28-31 (pos 27-30, 4 chars, degrees ×10)
            rwy.end1.heading_deg = parseInt(&line[27], 4) / 10.0;

            // Threshold lat/lon: cols 33-51 (pos 32-50, 9+10 chars)
            if (line[32] == 'N' || line[32] == 'S') {
                parseA424LatLon(&line[32], rwy.end1.threshold_lat_rad,
                                rwy.end1.threshold_lon_rad);
            }

            // Landing threshold elevation: cols 52-56 (pos 51-55, 5 chars, signed feet)
            if (len > 55) {
                rwy.end1.elevation_m = parseInt(&line[51], 5) * FT2M;
                rwy.end2.elevation_m = rwy.end1.elevation_m;
            }

            // Displaced threshold: cols 60-63 (pos 59-62, 4 chars, signed feet)
            if (len > 62) {
                rwy.end1.displaced_threshold_m = std::abs(parseInt(&line[59], 4)) * FT2M;
            }

            // Compute reciprocal designator
            if (!rwy_id.empty()) {
                int num = 0;
                size_t i = 0;
                while (i < rwy_id.size() && std::isdigit(rwy_id[i])) {
                    num = num * 10 + (rwy_id[i] - '0');
                    i++;
                }
                std::string suffix;
                if (i < rwy_id.size()) suffix = rwy_id.substr(i);
                int recip_num = (num <= 18) ? num + 18 : num - 18;
                std::string recip_suffix;
                if (suffix == "L") recip_suffix = "R";
                else if (suffix == "R") recip_suffix = "L";
                else recip_suffix = suffix;
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%02d%s", recip_num, recip_suffix.c_str());
                rwy.end2.designator = buf;
                rwy.end2.heading_deg = std::fmod(rwy.end1.heading_deg + 180.0, 360.0);
            }

            it->second.runways.push_back(rwy);
        }
    }
    fclose(f);

    // Merge reciprocal runway records: if two runways share the same strip,
    // the A424 file has separate PG records for each end. Match them up.
    for (auto & [icao, apt] : airports_) {
        // Build map: designator → index
        std::unordered_map<std::string, size_t> desig_idx;
        for (size_t i = 0; i < apt.runways.size(); ++i) {
            desig_idx[apt.runways[i].end1.designator] = i;
        }
        // For each runway, if its end2 designator matches another runway's end1,
        // fill in end2 from that record and mark the other for removal
        std::vector<bool> remove(apt.runways.size(), false);
        for (size_t i = 0; i < apt.runways.size(); ++i) {
            if (remove[i]) continue;
            auto jt = desig_idx.find(apt.runways[i].end2.designator);
            if (jt != desig_idx.end() && jt->second != i && !remove[jt->second]) {
                auto & other = apt.runways[jt->second];
                apt.runways[i].end2.threshold_lat_rad = other.end1.threshold_lat_rad;
                apt.runways[i].end2.threshold_lon_rad = other.end1.threshold_lon_rad;
                apt.runways[i].end2.elevation_m = other.end1.elevation_m;
                apt.runways[i].end2.heading_deg = other.end1.heading_deg;
                apt.runways[i].end2.displaced_threshold_m = other.end1.displaced_threshold_m;
                remove[jt->second] = true;
            }
        }
        // Erase merged duplicates
        auto new_end = std::remove_if(apt.runways.begin(), apt.runways.end(),
            [&](const Runway & r) {
                size_t idx = &r - apt.runways.data();
                return remove[idx];
            });
        apt.runways.erase(new_end, apt.runways.end());
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// XP12 loader (apt.dat)
// ─────────────────────────────────────────────────────────────────────────────

bool AirportDatabase::loadXP12(const std::string & path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    Airport current;
    bool has_current = false;

    auto flush = [&]() {
        if (has_current && !current.icao.empty()) {
            airports_[current.icao] = std::move(current);
        }
        current = Airport{};
        has_current = false;
    };

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;

        // Parse row code
        std::istringstream iss(line);
        int code = -1;
        if (!(iss >> code)) continue;

        switch (code) {
        case 1:   // Airport header
        case 16:  // Seaplane base
        case 17:  // Heliport
        {
            flush();
            has_current = true;
            int elev_ft, has_twr, default_sign;
            std::string icao, rest;
            if (!(iss >> elev_ft >> has_twr >> default_sign >> icao))
                break;
            current.icao = icao;
            current.elevation_m = elev_ft * FT2M;
            // Name is the rest of the line
            std::getline(iss, rest);
            size_t s = rest.find_first_not_of(" \t");
            if (s != std::string::npos)
                current.name = rest.substr(s);
            break;
        }
        case 1302:  // Metadata
        {
            if (!has_current) break;
            std::string key, value;
            if (!(iss >> key)) break;
            std::getline(iss, value);
            size_t s = value.find_first_not_of(" \t");
            if (s != std::string::npos) value = value.substr(s);

            if (key == "city")               current.city = value;
            else if (key == "country")       current.country = value;
            else if (key == "iata_code")     current.iata = value;
            else if (key == "datum_lat")     current.arp_lat_rad = std::stod(value) * DEG2RAD;
            else if (key == "datum_lon")     current.arp_lon_rad = std::stod(value) * DEG2RAD;
            else if (key == "transition_alt") current.transition_altitude_ft = std::stod(value);
            break;
        }
        case 100:  // Land runway
        {
            if (!has_current) break;
            // 100 width surface shoulder smoothness centerline edge_lights auto_signs
            //     rw1_desig rw1_lat rw1_lon rw1_displ rw1_stopway ...
            //     rw2_desig rw2_lat rw2_lon rw2_displ rw2_stopway ...
            double width;
            int surface, shoulder;
            double smoothness;
            int centerline, edge_lights, auto_signs;
            std::string d1, d2;
            double lat1, lon1, lat2, lon2;
            double displ1, stop1, displ2, stop2;
            // Skip already-read code
            // Fields after code: width surface shoulder smoothness centerline edge auto
            //   d1 lat1 lon1 displ1 stop1 [markings1 approach_lights1 tdz1 reil1]
            //   d2 lat2 lon2 displ2 stop2 [markings2 approach_lights2 tdz2 reil2]
            if (!(iss >> width >> surface >> shoulder >> smoothness
                      >> centerline >> edge_lights >> auto_signs
                      >> d1 >> lat1 >> lon1 >> displ1 >> stop1))
                break;
            // Skip optional markings/lighting fields for end1 (4 ints)
            int dummy;
            iss >> dummy >> dummy >> dummy >> dummy;
            if (!(iss >> d2 >> lat2 >> lon2 >> displ2 >> stop2))
                break;

            Runway rwy;
            rwy.width_m = width;
            rwy.surface_type = surface;
            rwy.end1.designator = d1;
            rwy.end1.threshold_lat_rad = xpLatLonToRad(lat1);
            rwy.end1.threshold_lon_rad = xpLatLonToRad(lon1);
            rwy.end1.displaced_threshold_m = displ1 * FT2M;
            rwy.end2.designator = d2;
            rwy.end2.threshold_lat_rad = xpLatLonToRad(lat2);
            rwy.end2.threshold_lon_rad = xpLatLonToRad(lon2);
            rwy.end2.displaced_threshold_m = displ2 * FT2M;

            // Compute length from threshold positions (great-circle)
            double dlat = lat2 - lat1;
            double dlon = lon2 - lon1;
            double avg_lat_rad = (lat1 + lat2) / 2.0 * DEG2RAD;
            double north_m = dlat * DEG2RAD * 6378137.0;
            double east_m = dlon * DEG2RAD * 6378137.0 * std::cos(avg_lat_rad);
            rwy.length_m = std::sqrt(north_m * north_m + east_m * east_m);

            // Compute headings from threshold positions
            double hdg_rad = std::atan2(east_m, north_m);
            rwy.end1.heading_deg = std::fmod(hdg_rad / DEG2RAD + 360.0, 360.0);
            rwy.end2.heading_deg = std::fmod(rwy.end1.heading_deg + 180.0, 360.0);

            rwy.end1.elevation_m = current.elevation_m;
            rwy.end2.elevation_m = current.elevation_m;

            current.runways.push_back(rwy);
            break;
        }
        default:
            break;
        }
    }
    flush();  // last airport

    // For airports without datum_lat/lon, derive from first runway
    for (auto & [icao, apt] : airports_) {
        if (apt.arp_lat_rad == 0.0 && apt.arp_lon_rad == 0.0 && !apt.runways.empty()) {
            auto & r = apt.runways[0];
            apt.arp_lat_rad = (r.end1.threshold_lat_rad + r.end2.threshold_lat_rad) / 2.0;
            apt.arp_lon_rad = (r.end1.threshold_lon_rad + r.end2.threshold_lon_rad) / 2.0;
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Query
// ─────────────────────────────────────────────────────────────────────────────

const Airport * AirportDatabase::findByICAO(const std::string & icao) const
{
    auto it = airports_.find(toUpper(icao));
    return (it != airports_.end()) ? &it->second : nullptr;
}

std::vector<const Airport *> AirportDatabase::search(const std::string & query, size_t max_results) const
{
    if (query.size() < 2) return {};

    std::string q = toUpper(query);
    std::vector<const Airport *> results;

    // Phase 1: ICAO prefix match
    for (auto & [icao, apt] : airports_) {
        if (icao.rfind(q, 0) == 0) {
            results.push_back(&apt);
            if (results.size() >= max_results) return results;
        }
    }

    // Phase 2: partial name match (case-insensitive)
    for (auto & [icao, apt] : airports_) {
        std::string upper_name = toUpper(apt.name);
        if (upper_name.find(q) != std::string::npos) {
            // Avoid duplicates from phase 1
            bool dup = false;
            for (auto * r : results) if (r->icao == icao) { dup = true; break; }
            if (!dup) {
                results.push_back(&apt);
                if (results.size() >= max_results) return results;
            }
        }
    }

    return results;
}

size_t AirportDatabase::runwayCount() const
{
    size_t count = 0;
    for (auto & [icao, apt] : airports_) count += apt.runways.size();
    return count;
}
