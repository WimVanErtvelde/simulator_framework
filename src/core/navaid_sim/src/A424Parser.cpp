#include "A424Parser.h"
#include "Globals.h"
#include <cstdio>

using namespace std;

namespace A424 {

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

A424Parser::A424Parser()
{

}

A424Parser::~A424Parser()
{

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool A424Parser::ParseA424(std::string filename, AS::World* pWorld)
{
	FILE* strIn = fopen(filename.c_str(), "r");
	if (!strIn)
		return false;

	size_t i = 1;
	char record[150];
	while (fgets(record, 150, strIn)) {
		if ((record[4] == 'P') && (record[5] == ' ') && (record[12] == 'A')) {
			//Airport
		}
		else if ((record[4] == 'P') && (record[5] == ' ') && (record[12] == 'G')) {
			//Runway
		}
		else if ((record[4] == 'P') && (record[5] == ' ') && (record[12] == 'V')) {
			//Airport Communication facilities
		}
		else if ((record[4] == 'P') && (record[5] == ' ') && (record[12] == 'I')) {
			//Airport Localizer/Glide slope
			if (!ParseLOCGSRecord(record, pWorld))
				LogMessage("ERROR parsing LOC/GS record line %d", i);
		}
		else if (((record[4] == 'E') && (record[5] == 'A') && (record[12] == ' ')) || ((record[4] == 'P') && (record[5] == ' ') && (record[12] == 'C'))) {
			//Isecs
		}
		else if ((record[4] == 'D') && (record[5] == ' ') && (record[12] == ' ')) {
			//VHF Navaids
			if (!ParseVHFRecord(record, pWorld))
				LogMessage("ERROR parsing VHF record line %d", i);
		}
		else if ((record[4] == 'D' && record[5] == 'B' && record[12] == ' ') || (record[4] == 'P' && record[5] == 'N' && record[12] == ' ')) {
			//NDB Navaids
			if (!ParseNDBRecord(record, pWorld))
				LogMessage("ERROR parsing NDB record line %d", i);
		}
		else if (record[4] == 'E' && record[5] == 'R') {
			// Airways
		}
		else if ((record[4] == 'P' && record[5] == ' ' && record[12] == 'D') || (record[4] == 'P' && record[5] == ' ' && record[12] == 'E') || (record[4] == 'P' && record[5] == ' ' && record[12] == 'F')) {
			// Procedures
		}
		else if (record[4] == 'E' && record[5] == 'V') {
			//Enroute Communication facilities
		}
		else if (record[4] == 'U' && record[5] == 'C') {
			//CONTROLLED AIRSPACES
		}
		else if (record[4] == 'U' && record[5] == 'R') {
			//RESTRICTIVE AIRSPACES
		}
		else if ((record[4] == 'P') && (record[5] == ' ') && (record[12] == 'P')) {
			//Airport SBAS Path Point Records
		}
		else if ((record[4] == 'P') && (record[5] == ' ') && (record[12] == 'M')) {
			//Airport Marker Records
			if (!ParseLOCMarkerRecord(record, pWorld))
				LogMessage("ERROR parsing LOC Marker record line %d", i);
		}
		i++;
	}

	fclose(strIn);
	return true;
}

A424Header A424Parser::ReadHeader(const std::string& filename)
{
	A424Header hdr;
	FILE* f = fopen(filename.c_str(), "r");
	if (!f) return hdr;

	char line[150];
	if (fgets(line, 150, f)) {
		// HDR01 record: CycleDate at [35..38], CreationDate at [41..51], Supplier at [60..75]
		if (line[0] == 'H' && line[1] == 'D' && line[2] == 'R') {
			hdr.cycle   = GetSubstrString(line + 35, 4);
			hdr.created = GetSubstrString(line + 41, 11);
			hdr.supplier = GetSubstrString(line + 60, 16);
			hdr.valid = true;
		}
	}
	fclose(f);
	return hdr;
}

bool A424Parser::ParseVHFRecord(char* record, AS::World* pWorld)
{
	VHFNavaidRecord* vhfNavaidRec = (VHFNavaidRecord*)record;
	if ((vhfNavaidRec->ContinuationRecordNumber == '0') || (vhfNavaidRec->ContinuationRecordNumber == '1')) {
		string id = GetSubstrString(vhfNavaidRec->VORIdentifier, 4);
		trimSTL(id);

		char flag, ndb_flag;
		string navaid_class = GetSubstrString(vhfNavaidRec->NavaidClass, 5);
		NavAIDType type = GetNavAidTypeFromClass(navaid_class, flag, ndb_flag);
		if (type == UnknownNavAID)
			return false;
		(void)vhfNavaidRec->NavaidClass[3]; // addInfo — unused

		double lat, lon = 0.0;
		if (vhfNavaidRec->VORLatitude[0] != ' ')
			GetLatitudeLongitudeFromString(vhfNavaidRec->VORLatitude, lat, lon);
		else if (vhfNavaidRec->DMELatitude[0] != ' ')
			GetLatitudeLongitudeFromString(vhfNavaidRec->DMELatitude, lat, lon);

		string name = GetSubstrString(vhfNavaidRec->VORName, 30);
		trimSTL(name);

		//double freq = GetSubstrDouble(vhfNavaidRec->VORFrequency, 5) / 100.0;
		int freq = GetSubstrInt(vhfNavaidRec->VORFrequency, 5);

		int elev = GetSubstrInt(vhfNavaidRec->DMEElevation, 5);
		double bias = GetSubstrDouble(vhfNavaidRec->ILSDMEBias, 2);

		float range = 130.f;
		switch (vhfNavaidRec->NavaidClass[2]) {
		case 'T':
			range = 30.f;
			break;
		case 'L':
			range = 50.f;
			break;
		case 'H':
			range = 130.f;
			break;
		case 'U':
			range = 130.f;
			break;
		case 'C':
			range = 30.f;
			break;
		}

		double stationDeclination = 0;
		for (int i = 1; i < 5; ++i) {
			stationDeclination *= 10.0;
			stationDeclination += (vhfNavaidRec->StationDeclination[i] - '0');
		}
		stationDeclination /= (vhfNavaidRec->StationDeclination[0] == 'W' ? -10.0f : 10.0f);

		string icao_code1 = GetSubstrString(vhfNavaidRec->ICAOCode1, 2);
		string icao_code2 = GetSubstrString(vhfNavaidRec->ICAOCode2, 2);
		string area_code = GetSubstrString(vhfNavaidRec->AreaCode, 3);
		//pair<string, string> country = GetCountryFromCode(icao_code2);

		switch (type) {
		case UnknownNavAID:
			break;
		case DME:
			{
				AS::DME dme(static_cast<float>(lat), static_cast<float>(lon), static_cast<float>(elev), freq, range, static_cast<float>(bias), id, name);
				/*if (name == "DUTCH HARBOR")
					printf("");*/
				pWorld->addDME(dme);
			}
			break;
		case NDB:
			break;
		case TACAN:
			break;		
		case VORD:
			{
				AS::DME dme(static_cast<float>(lat), static_cast<float>(lon), static_cast<float>(elev), freq, range, static_cast<float>(bias), id, name);
				/*if (name == "DUTCH HARBOR")
					printf("");*/
				pWorld->addDME(dme);
			}
		__fallthrough;
		case VOR:
		case VORT:
			{
				AS::VOR vor(static_cast<float>(lat), static_cast<float>(lon), static_cast<float>(elev), freq, range, static_cast<float>(stationDeclination), id, name);
				/*if (name == "NICKY")
					printf("");*/
				pWorld->addVOR(vor);
			}
			break;
		case LOC:
			break;
		case GS:
			break;
		case ILS:
			break;
		case ILSD:
			{
				AS::DME dme(static_cast<float>(lat), static_cast<float>(lon), static_cast<float>(elev), freq, range, static_cast<float>(bias), id, name);
				/*if (name == "DUTCH HARBOR")
					printf("");*/
				pWorld->addDME(dme);
			}
			break;
		case GLS:
			break;
		case OuterMarker:
			break;
		case MiddleMarker:
			break;
		case InnerMarker:
			break;
		case BackMarker:
			break;
		}
	}

	return true;
}

bool A424Parser::ParseNDBRecord(char* record, AS::World* pWorld)
{
	NDBRecord* ndbRec = (NDBRecord*)record;
	if ((ndbRec->ContinuationRecordNumber == '0') || (ndbRec->ContinuationRecordNumber == '1')) {
		string id = GetSubstrString(ndbRec->NDBIdentifier, 4);
		trimSTL(id);

		char flag, ndb_flag;
		string navaid_class = GetSubstrString(ndbRec->NDBClass, 5);
		NavAIDType type = GetNavAidTypeFromClass(navaid_class, flag, ndb_flag);
		if (type == UnknownNavAID)
			return false;

		double lat, lon;
		GetLatitudeLongitudeFromString(ndbRec->NDBLatitude, lat, lon);

		/*string name = GetSubstrString(ndbRec->NDBName, 30);
		trimSTL(name);*/
		pair<string, string> city_name = GetNameAndCity(GetSubstrString(ndbRec->NDBName, 30));
		string city = city_name.first;
		string name = city_name.second;

		double freq = GetSubstrDouble(ndbRec->NDBFrequency, 5) / 10.0;

		string icao_code1 = GetSubstrString(ndbRec->ICAOCode1, 2);
		string icao_code2 = GetSubstrString(ndbRec->ICAOCode2, 2);
		string area_code = GetSubstrString(ndbRec->AreaCode, 3);
		//pair<string, string> country = GetCountryFromCode(icao_code2);

		float range = 130.f;
		switch (ndbRec->NDBClass[2]) {
		case 'T':
			range = 25.f;
			break;
		case 'L':
			range = 40.f;
			break;
		case 'H':
			range = 130.f;
			break;
		case 'U':
			range = 130.f;
			break;
		case 'C':
			range = 25.f;
			break;
		}

		AS::NDB ndb(static_cast<float>(lat), static_cast<float>(lon), static_cast<int>(freq), range, id, name);
		pWorld->addNDB(ndb);
		/*if (name == "ASMARA")
			printf("");*/
	}
	return true;
}

bool A424Parser::ParseLOCGSRecord(char* record, AS::World* pWorld)
{
	LOCGSRecord* locGsRec = (LOCGSRecord*)record;
	if ((locGsRec->ContinuationRecordNumber == '0') || (locGsRec->ContinuationRecordNumber == '1')) {
		string airport_id = GetSubstrString(locGsRec->AirportICAOIdentifier, 4);

		string id = GetSubstrString(locGsRec->LocalizerIdentifier, 4);
		trimSTL(id);

		ILSCategory category = GetILSCategory(locGsRec->ILSCategory);
		//double freq = GetSubstrDouble(locGsRec->LocalizerFrequency, 5) / 100.0;
		int freq = GetSubstrInt(locGsRec->LocalizerFrequency, 5);

		string rwId = GetSubstrString(locGsRec->RunwayIdentifier, 5);
		if ((rwId.size() > 2) && (rwId[0] == 'R') && (rwId[1] == 'W'))
			rwId = rwId.substr(2);

		double loc_lat = 0.0;
		double loc_lon = 0.0;
		if (locGsRec->LocalizerLatitude[0] != ' ')
			GetLatitudeLongitudeFromString(locGsRec->LocalizerLatitude, loc_lat, loc_lon);

		double loc_magnetic_bearing = GetSubstrDouble(locGsRec->LocalizerBearing, 4) / 10.0;

		double gs_lat = 0.0;
		double gs_lon = 0.0;
		if (locGsRec->GlideSlopeLatitude[0] != ' ')
			GetLatitudeLongitudeFromString(locGsRec->GlideSlopeLatitude, gs_lat, gs_lon);

		double stationDeclination = 0;
		for (int i = 1; i < 5; ++i) {
			stationDeclination *= 10.0;
			stationDeclination += (locGsRec->StationDeclination[i] - '0');
		}
		stationDeclination /= (locGsRec->StationDeclination[0] == 'W' ? -10.0f : 10.0f);

		double gs_angle = GetSubstrDouble(locGsRec->GlideSlopeAngle, 3) / 100.0;
		double gs_elevation = GetSubstrDouble(locGsRec->GlideSlopeElevation, 5);
		(void)GetSubstrDouble(locGsRec->GlideSlopeHeightatLandingThreshold, 3); // gs_height_at_th — unused


		if ((loc_lat != 0.0) && (loc_lon != 0.0)) {
			string name = "";
			switch (category) {
			case ILSNoGS:
				name = "ILS";
				break;
			case ILSCat1:
				name = "ILS-cat-I";
				break;
			case ILSCat2:
				name = "ILS-cat-II";
				break;
			case ILSCat3:
				name = "ILS-cat-III";
				break;
			case IGS:
				name = "IGS";
				break;
			case LDA:
				name = "LDA";
				break;
			case LDAGS:
				name = "LDA-GS";
				break;
			case SDF:
				name = "SDF";
				break;
			case SDFGS:
				name = "SDF-GS";
				break;
			case NoILS:
				name = "";
				break;
			}
			double loc_true_bearing = WrapTrack(loc_magnetic_bearing - stationDeclination);
			AS::ILS_LOC loc(static_cast<float>(loc_lat), static_cast<float>(loc_lon), static_cast<float>(gs_elevation), freq, 25.f, static_cast<float>(loc_true_bearing), static_cast<float>(loc_magnetic_bearing), id, airport_id, rwId, name);
			pWorld->addILS_LOC(loc);
			/*if ((airport_id == "EBBR") && (id == "IBL"))
				printf("");*/
		}

		if ((gs_lat != 0.0) && (gs_lon != 0.0)) {
			string name = "GS";
			AS::ILS_GS gs(static_cast<float>(gs_lat), static_cast<float>(gs_lon), static_cast<float>(gs_elevation), freq, 25.f, static_cast<float>(loc_magnetic_bearing), static_cast<float>(gs_angle), id, airport_id, rwId, name);
			pWorld->addILS_GS(gs);
			/*if ((airport_id == "EBBR") && (id == "IBL"))
				printf("");*/
		}
	}

	return true;
}

bool A424Parser::ParseLOCMarkerRecord(char* record, AS::World* pWorld)
{
	LOCMarkerRecord* locMarkerRec = (LOCMarkerRecord*)record;
	if ((locMarkerRec->ContinuationRecordNumber == '0') || (locMarkerRec->ContinuationRecordNumber == '1')) {
		string airport_id = GetSubstrString(locMarkerRec->AirportICAOIdentifier, 4);

		string loc_id = GetSubstrString(locMarkerRec->LocalizerIdentifier, 4);
		trimSTL(loc_id);

		string rwId = GetSubstrString(locMarkerRec->RunwayIdentifier, 5);
		if ((rwId.size() > 2) && (rwId[0] == 'R') && (rwId[1] == 'W'))
			rwId = rwId.substr(2);

		double mkr_lat = 0.0;
		double mkr_lon = 0.0;
		if (locMarkerRec->MarkerLatitude[0] != ' ')
			GetLatitudeLongitudeFromString(locMarkerRec->MarkerLatitude, mkr_lat, mkr_lon);

		double loc_lat = 0.0;
		double loc_lon = 0.0;
		if (locMarkerRec->LocatorLatitude[0] != ' ')
			GetLatitudeLongitudeFromString(locMarkerRec->LocatorLatitude, loc_lat, loc_lon);

		double elevation = GetSubstrDouble(locMarkerRec->FacilityElevation, 5);

		std::string type = GetSubstrString(&locMarkerRec->MarkerType[1], 2);

		double true_bearing = GetSubstrDouble(locMarkerRec->MinorAxisBearing, 4) / 10.0;

		AS::ILS_Marker marker(static_cast<float>(mkr_lat), static_cast<float>(mkr_lon), static_cast<float>(elevation), static_cast<float>(true_bearing), airport_id, rwId, type);
		pWorld->addILS_Marker(marker);
		/*if ((airport_id == "EBBR") && (loc_id == "IBL"))
			printf("");*/
	}
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int A424Parser::GetSubstrInt(char* buf, size_t len)
{
	char* dest = new char[len + 1];
	strncpy(dest, buf, len);
	dest[len] = '\0';
	int ret = atoi(dest);
	delete[] dest;
	return ret;
}

float A424Parser::GetSubstrFloat(char* buf, size_t len)
{
	char* dest = new char[len + 1];
	strncpy(dest, buf, len);
	dest[len] = '\0';
	float ret = (float)atof(dest);
	delete[] dest;
	return ret;
}

double A424Parser::GetSubstrDouble(char* buf, size_t len)
{
	char* dest = new char[len + 1];
	strncpy(dest, buf, len);
	dest[len] = '\0';
	double ret = atof(dest);
	delete[] dest;
	return ret;
}

string A424Parser::GetSubstrString(char* buf, size_t len)
{
	char* dest = new char[len + 1];
	strncpy(dest, buf, len);
	dest[len] = '\0';
	string ret = dest;
	delete[] dest;
	return ret;
}

void A424Parser::GetLatitudeLongitudeFromString(char* data, double& latitude, double& longitude)
{
	//Latitude;
	long sign;
	if (data[0] == 'N') { sign = 1; }
	else if (data[0] == 'S') { sign = -1; }
	else sign = 0;

	double seconds = 0;
	for (int i = 5; i <= 8; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		seconds *= 10;
		seconds += (data[i] - '0');
	}
	seconds /= 100.0;
	double minutes = 0;
	for (int i = 3; i <= 4; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		minutes *= 10;
		minutes += (data[i] - '0');
	}
	double degrees = 0;
	for (int i = 1; i <= 2; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		degrees *= 10;
		degrees += (data[i] - '0');
	}
	minutes = minutes + (seconds / 60.0);
	degrees = degrees + (minutes / 60.0);
	long ilatitude = static_cast<long>((degrees / 360.0) * 16777216.0) * sign;
	latitude = (static_cast<double>(ilatitude) / 16777216.0 * 360.0);

	if (data[9] == 'E') { sign = 1; }
	else if (data[9] == 'W') { sign = -1; }
	else sign = 0;
	seconds = 0;
	for (int i = 15; i <= 18; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		seconds *= 10;
		seconds += (data[i] - '0');
	}
	seconds /= 100.0;
	minutes = 0;
	for (int i = 13; i <= 14; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		minutes *= 10;
		minutes += (data[i] - '0');
	}
	degrees = 0;
	for (int i = 10; i <= 12; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		degrees *= 10;
		degrees += (data[i] - '0');
	}
	minutes = minutes + (seconds / 60.0);
	degrees = degrees + (minutes / 60.0);
	long ilongitude = static_cast<long>((degrees / 360.0) * 16777216.0) * sign;
	longitude = (static_cast<double>(ilongitude) / 16777216.0 * 360.0);
}

void A424Parser::GetLatitudeLongitudeFromStringHP(char* data, double& latitude, double& longitude)
{
	//Latitude;
	long sign;
	if (data[0] == 'N') { sign = 1; }
	else if (data[0] == 'S') { sign = -1; }
	else sign = 0;

	double seconds = 0;
	for (int i = 5; i <= 10; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		seconds *= 10;
		seconds += (data[i] - '0');
	}
	seconds /= 10000.0;
	double minutes = 0;
	for (int i = 3; i <= 4; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		minutes *= 10;
		minutes += (data[i] - '0');
	}
	double degrees = 0;
	for (int i = 1; i <= 2; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		degrees *= 10;
		degrees += (data[i] - '0');
	}
	minutes = minutes + (seconds / 60.0);
	degrees = degrees + (minutes / 60.0);
	long ilatitude = static_cast<long>((degrees / 360.0) * 16777216.0) * sign;
	latitude = (static_cast<double>(ilatitude) / 16777216.0 * 360.0);

	if (data[11] == 'E') { sign = 1; }
	else if (data[11] == 'W') { sign = -1; }
	else sign = 0;
	seconds = 0;
	for (int i = 17; i <= 22; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		seconds *= 10;
		seconds += (data[i] - '0');
	}
	seconds /= 10000.0;
	minutes = 0;
	for (int i = 15; i <= 16; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		minutes *= 10;
		minutes += (data[i] - '0');
	}
	degrees = 0;
	for (int i = 12; i <= 14; ++i)
	{
		if (data[i] < '0' || data[i] > '9') sign = 0;
		degrees *= 10;
		degrees += (data[i] - '0');
	}
	minutes = minutes + (seconds / 60.0);
	degrees = degrees + (minutes / 60.0);
	long ilongitude = static_cast<long>((degrees / 360.0) * 16777216.0) * sign;
	longitude = (static_cast<double>(ilongitude) / 16777216.0 * 360.0);
}

NavAIDType A424Parser::GetNavAidTypeFromClass(string navaidClass, char& flag, char& ndb_flag)
{
	NavAIDType type = UnknownNavAID;
	flag = ' ';
	ndb_flag = ' ';
	switch (navaidClass[0]) {
	case 'V':
	{
		switch (navaidClass[1]) {
		case ' ':
			type = VOR;
			break;
		case 'D':
			type = VORD;
			break;
		case 'T':
		case 'M':
			type = VORT;
			break;
		case 'I':
			type = ILSD;
			break;
			/*case 'N':
				printf("hehe");
				break;
			case 'P':
				printf("hehe");
				break;*/
		}
	}
	break;
	case ' ':
	{
		switch (navaidClass[1]) {
			/*case ' ':
				printf("hehe");
				break;*/
		case 'D':
			type = DME;
			break;
		case 'T':
		case 'M':
			type = TACAN;
			break;
		case 'I':
			type = ILSD;
			break;
			/*case 'N':
				printf("hehe");
				break;
			case 'P':
				printf("hehe");
				break;*/
		}
	}
	break;
	case 'H':
	case 'S':
	case 'M':
	{
		ndb_flag = navaidClass[1];
		type = NDB;
	}
	break;
	}

	flag = navaidClass[2];

	return type;
}

ILSCategory A424Parser::GetILSCategory(char code)
{
	switch (code) {
	case '0':
		return ILSNoGS;
		break;
	case '1':
		return ILSCat1;
		break;
	case '2':
		return ILSCat2;
		break;
	case '3':
		return ILSCat3;
		break;
	case 'I':
		return IGS;
		break;
	case 'A':
		return LDA;
		break;
	case 'L':
		return LDAGS;
		break;
	case 'F':
		return SDF;
		break;
	case 'S':
		return SDFGS;
		break;
	}
	return NoILS;
}

pair<string, string> A424Parser::GetNameAndCity(std::string ap_city)
{
	string city = "";
	string name = "";

	vector<string> city_name = splitSTLEx(ap_city, '/');
	if (city_name.size() >= 2) {
		city = city_name[0];
		name = city_name[1];
	}
	else {
		city = city_name[0];
		name = city_name[0];

		string::size_type pos = city.find("NATL");
		if (pos == string::npos)
			pos = city.find("NATIONAL");
		if (pos == string::npos)
			pos = city.find("INTL");
		if (pos == string::npos)
			pos = city.find("INTERNATIONAL");
		if (pos == string::npos)
			pos = city.find("(");

		if (pos != string::npos) {
			city = name;
			city.erase(pos);
		}
	}
	trimSTL(city);
	trimSTL(name);

	return pair<string, string>(city, name);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // A424