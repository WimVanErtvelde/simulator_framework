#include "WorldParser.h"
#include <sstream>
#include <iostream>
#include <fstream>
#include <thread>
#ifdef linux
#include <stdlib.h>
#endif

#include "VOR.h"
#include "ILS-LOC.h"
#include "ILS-GS.h"
#include "DME.h"
#include "World.h"
#include "MagDec.h"

AS::World AS::WorldParser::parse(std::string filename)
{
	AS::World world;
	// open the file
	std::ifstream input(filename.c_str());
	if(input)
	{
		// read all the lines
		std::string line;
		while (getline(input, line))
		{
			std::istringstream ss(line);  
			std::string element;
			
			getline(ss,element,',');
			if(element.compare("VOR") == 0)
			{
				getline(ss,element,',');
				float lat = (float)atof(element.c_str());

				getline(ss,element,',');
				float lon = (float)atof(element.c_str());

				getline(ss,element,',');
				float ele = (float)atof(element.c_str());

				getline(ss,element,',');
				int freq = atoi(element.c_str());

				getline(ss,element,',');
				float range = (float)atof(element.c_str());

				getline(ss,element,',');
				float variation = (float)atof(element.c_str());

				getline(ss,element,',');
				std::string ident(element);

				getline(ss,element,',');
				std::string name(element);

				VOR vor(lat,lon,ele,freq,range,variation,ident,name);
				world.addVOR(vor);
			}
			else if (element.compare("NDB") == 0)
			{
				getline(ss, element, ',');
				float lat = (float)atof(element.c_str());

				getline(ss, element, ',');
				float lon = (float)atof(element.c_str());

				getline(ss, element, ',');
				//float ele = (float)atof(element.c_str());

				getline(ss, element, ',');
				int freq = atoi(element.c_str());

				getline(ss, element, ',');
				float range = (float)atof(element.c_str());

				getline(ss, element, ',');
				//float variation = (float)atof(element.c_str());

				getline(ss, element, ',');
				std::string ident(element);

				getline(ss, element, ',');
				std::string name(element);

				NDB ndb(lat, lon, freq, range, ident, name);
				world.addNDB(ndb);
			}
			else if(element.compare("ILS-LOC") == 0)
			{
				getline(ss,element,',');
				float lat = (float)atof(element.c_str());

				getline(ss,element,',');
				float lon = (float)atof(element.c_str());

				getline(ss,element,',');
				float ele = (float)atof(element.c_str());

				getline(ss,element,',');
				int freq = atoi(element.c_str());

				getline(ss,element,',');
				float range = (float)atof(element.c_str());

				getline(ss,element,',');
				float bearing = (float)atof(element.c_str());

				getline(ss,element,',');
				std::string ident(element);

				getline(ss,element,',');
				std::string apt(element);
				
				getline(ss,element,',');
				std::string rw(element);

				getline(ss,element,',');
				std::string name(element);

				ILS_LOC loc(lat,lon,ele,freq,range,bearing, bearing,ident,apt,rw,name);
				world.addILS_LOC(loc);
			}

			else if(element.compare("ILS-GS") == 0)
			{
				getline(ss,element,',');
				float lat = (float)atof(element.c_str());

				getline(ss,element,',');
				float lon = (float)atof(element.c_str());

				getline(ss,element,',');
				float ele = (float)atof(element.c_str());

				getline(ss,element,',');
				int freq = atoi(element.c_str());

				getline(ss,element,',');
				float range = (float)atof(element.c_str());

				getline(ss,element,',');
				float bearing = (float)atof(element.c_str());

				getline(ss,element,',');
				float angle = (float)atof(element.c_str());

				getline(ss,element,',');
				std::string ident(element);

				getline(ss,element,',');
				std::string apt(element);
				
				getline(ss,element,',');
				std::string rw(element);

				getline(ss,element,',');
				std::string name(element);

				ILS_GS gs(lat,lon,ele,freq,range,angle,bearing,ident,apt,rw,name);
				world.addILS_GS(gs);
			}

			else if(element.compare("ILS-M") == 0)
			{
				getline(ss,element,',');
				float lat = (float)atof(element.c_str());

				getline(ss,element,',');
				float lon = (float)atof(element.c_str());

				getline(ss,element,',');
				float ele = (float)atof(element.c_str());

				getline(ss,element,',');
				float heading = (float)atof(element.c_str());

				getline(ss,element,',');
				std::string airport(element);

				getline(ss,element,',');
				std::string runway(element);
				
				getline(ss,element,',');
				std::string type(element);

				ILS_Marker marker(lat,lon,ele,heading,airport,runway,type);
				world.addILS_Marker(marker);
			}

			else if (element.compare("DME") == 0)
			{
				getline(ss, element, ',');
				float lat = (float)atof(element.c_str());

				getline(ss, element, ',');
				float lon = (float)atof(element.c_str());

				getline(ss, element, ',');
				float ele = (float)atof(element.c_str());

				getline(ss, element, ',');
				int freq = atoi(element.c_str());

				getline(ss, element, ',');
				float range = (float)atof(element.c_str());

				getline(ss, element, ',');
				float bias = (float)atof(element.c_str());

				getline(ss, element, ',');
				std::string ident(element);

				getline(ss, element, ',');
				std::string name(element);

				DME dme(lat, lon, ele, freq, range, bias, ident, name);
				world.addDME(dme);
			}
		}

		// close the file
		input.close();
	}

	return world;
}

std::vector<std::string> splitString(const std::string& input, std::vector<std::string>& result) {
	result.clear();
	std::istringstream ss(input);
	std::string tok;
	while (ss >> tok)
		result.push_back(tok);
	return result;
}

void AS::WorldParser::parseXP12(std::string filename, AS::World& world, const MagDec* magdec)
{
	// open the file
	std::ifstream input(filename.c_str());
	if (input)
	{
		// read all the lines
		std::string line;
		std::vector<std::string> tokens;
		while (getline(input, line)) {
			splitString(line, tokens);
			if (tokens.empty()) continue;

			int typeCode = atoi(tokens[0].c_str());

			// XP810 format has one fewer column than XP12 for VOR/NDB (no region)
			// and LOC/GS/Marker (no extra column between airport and runway).
			// Auto-detect from token count: XP810 VOR=10, XP12 VOR=11+.

			if (typeCode == 3 && tokens.size() >= 10) { // VOR
				float lat = (float)atof(tokens[1].c_str());
				float lon = (float)atof(tokens[2].c_str());
				float ele = (float)atof(tokens[3].c_str());
				int freq = atoi(tokens[4].c_str());
				float range = (float)atof(tokens[5].c_str());
				float variation = (float)atof(tokens[6].c_str());
				std::string ident(tokens[7]);
				// XP12: name at tokens[10] (has airport+region cols at 8,9)
				// XP810: name at tokens[8] (no extra cols)
				std::string name = tokens.size() >= 11 ? tokens[10] : tokens[8];
				VOR vor(lat, lon, ele, freq, range, variation, ident, name);
				world.addVOR(vor);
			}
			else if (typeCode == 2 && tokens.size() >= 10) { // NDB
				float lat = (float)atof(tokens[1].c_str());
				float lon = (float)atof(tokens[2].c_str());
				int freq = atoi(tokens[4].c_str());
				float range = (float)atof(tokens[5].c_str());
				std::string ident(tokens[7]);
				std::string name = tokens.size() >= 11 ? tokens[10] : tokens[8];
				NDB ndb(lat, lon, freq, range, ident, name);
				world.addNDB(ndb);
			}
			else if ((typeCode == 4 || typeCode == 5) && tokens.size() >= 11) { // LOC
				float lat = (float)atof(tokens[1].c_str());
				float lon = (float)atof(tokens[2].c_str());
				float ele = (float)atof(tokens[3].c_str());
				int freq = atoi(tokens[4].c_str());
				float range = (float)atof(tokens[5].c_str());
				float truebearing = (float)atof(tokens[6].c_str());
				truebearing = fmod(truebearing, 360.f);
				// X-Plane only provides true bearing; compute magnetic via WMM grid
				float magnbearing = truebearing;
				if (magdec && magdec->isLoaded())
					magnbearing = fmod(truebearing - magdec->getDeclination(lat, lon) + 360.f, 360.f);
				std::string ident(tokens[7]);
				std::string apt(tokens[8]);
				// XP12: rw at [10], name at [11] (extra region col at [9])
				// XP810: rw at [9], name at [10]
				std::string rw, name;
				if (tokens.size() >= 12) { rw = tokens[10]; name = tokens[11]; }
				else                     { rw = tokens[9];  name = tokens[10]; }
				ILS_LOC loc(lat, lon, ele, freq, range, truebearing, magnbearing, ident, apt, rw, name);
				world.addILS_LOC(loc);
			}
			else if (typeCode == 6 && tokens.size() >= 11) { // GS
				float lat = (float)atof(tokens[1].c_str());
				float lon = (float)atof(tokens[2].c_str());
				float ele = (float)atof(tokens[3].c_str());
				int freq = atoi(tokens[4].c_str());
				float range = (float)atof(tokens[5].c_str());
				float bearing = (float)atof(tokens[6].c_str());
				float angle = float(int(bearing) / 1000) / 100.f;
				bearing = static_cast<float>(fmod(bearing, 1000));
				std::string ident(tokens[7]);
				std::string apt(tokens[8]);
				std::string rw, name;
				if (tokens.size() >= 12) { rw = tokens[10]; name = tokens[11]; }
				else                     { rw = tokens[9];  name = tokens[10]; }
				ILS_GS gs(lat, lon, ele, freq, range, bearing, angle, ident, apt, rw, name);
				world.addILS_GS(gs);
			}
			else if (typeCode >= 7 && typeCode <= 9 && tokens.size() >= 11) { // Marker
				float lat = (float)atof(tokens[1].c_str());
				float lon = (float)atof(tokens[2].c_str());
				float ele = (float)atof(tokens[4].c_str());
				float heading = (float)atof(tokens[6].c_str());
				std::string airport(tokens[8]);
				std::string runway, type;
				if (tokens.size() >= 12) { runway = tokens[10]; type = tokens[11]; }
				else                     { runway = tokens[9];  type = tokens[10]; }
				ILS_Marker marker(lat, lon, ele, heading, airport, runway, type);
				world.addILS_Marker(marker);
			}
			else if ((typeCode == 12 || typeCode == 13) && tokens.size() >= 10) { // DME
				float lat = (float)atof(tokens[1].c_str());
				float lon = (float)atof(tokens[2].c_str());
				float ele = (float)atof(tokens[3].c_str());
				int freq = atoi(tokens[4].c_str());
				float range = (float)atof(tokens[5].c_str());
				float bias = (float)atof(tokens[6].c_str());
				std::string ident(tokens[7]);
				std::string name = tokens.size() >= 11 ? tokens[10] : tokens[8];
				DME dme(lat, lon, ele, freq, range, bias, ident, name);
				world.addDME(dme);
			}
		}
	}
}
