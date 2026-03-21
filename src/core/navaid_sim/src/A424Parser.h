#pragma once

#include "World.h"
#include <string>

namespace A424 {


struct HeaderRecord {
	char HeaderIdent[3];
	char HeaderNumber[2];
	char FileName[15];
	char VersionNumber[3];
	char ProductionTestFlag;
	char RecordLength[4];
	char RecordCount[7];
	char CycleDate[4];
	char Blank1[2];
	char CreationDate[11];
	char CreationTime[8];
	char Blank2;
	char DataSupplierIdent[16];
	char TargetCustomerIdent[16];
	char DatabasePartNumber[20];
	char Reserved[11];
	char FileCRC[8];
	char Endline;
};

struct VHFNavaidRecord {
	char RecordType;
	char AreaCode[3];
	char SectionCode;
	char SubsectionCode;
	char AirportICAOIdentifier[4];
	char ICAOCode1[2];
	char Blank1;
	char VORIdentifier[4];
	char Blank2[2];
	char ICAOCode2[2];
	char ContinuationRecordNumber;
	char VORFrequency[5];
	char NavaidClass[5];
	char VORLatitude[9];
	char VORLongitude[10];
	char DMEIdent[4];
	char DMELatitude[9];
	char DMELongitude[10];
	char StationDeclination[5];
	char DMEElevation[5];
	char FigureOfMerit;
	char ILSDMEBias[2];
	char FrequencyProtection[3];
	char DatumCode[3];
	char VORName[30];
	char FileRecordNumber[5];
	char CycleDate[4];
	char Endline;
};

struct NDBRecord {
	char RecordType;
	char AreaCode[3];
	char SectionCode;
	char SubsectionCode;
	char AirportICAOIdentifier[4];
	char ICAOCode1[2];
	char Blank1;
	char NDBIdentifier[4];
	char Blank2[2];
	char ICAOCode2[2];
	char ContinuationRecordNumber;
	char NDBFrequency[5];
	char NDBClass[5];
	char NDBLatitude[9];
	char NDBLongitude[10];
	char Blank3[23];
	char MagneticVariation[5];
	char Blank4[6];
	char Reserved1[5];
	char DatumCode[3];
	char NDBName[30];
	char FileRecordNumber[5];
	char CycleDate[4];
	char Endline;
};

struct LOCGSRecord {
	char RecordType;
	char AreaCode[3];
	char SectionCode;
	char Blank1;
	char AirportICAOIdentifier[4];
	char ICAOCode1[2];
	char SubsectionCode;
	char LocalizerIdentifier[4];
	char ILSCategory;
	char Blank2[3];
	char ContinuationRecordNumber;
	char LocalizerFrequency[5];
	char RunwayIdentifier[5];
	char LocalizerLatitude[9];
	char LocalizerLongitude[10];
	char LocalizerBearing[4];
	char GlideSlopeLatitude[9];
	char GlideSlopeLongitude[10];
	char LocalizerPosition[4];
	char LocalizerPositionReference;
	char GlideSlopePosition[4];
	char LocalizerWidth[4];
	char GlideSlopeAngle[3];
	char StationDeclination[5];
	char Blank3[2];
	char GlideSlopeElevation[5];
	char SupportingFacilityID[4];
	char SupportingFacilityICAOCode[2];
	char SupportingFacilitySectionCode;
	char SupportingFacilitySubsectionCode;
	char GlideSlopeHeightatLandingThreshold[3];
	char Reserved[10];
	char FileRecordNo[5];
	char CycleDate[4];
};

struct LOCMarkerRecord {
	char RecordType;
	char AreaCode[3];
	char SectionCode;
	char Blank1;
	char AirportICAOIdentifier[4];
	char ICAOCode1[2];
	char SubsectionCode;
	char LocalizerIdentifier[4];
	char MarkerType[3];
	char Blank2;
	char ContinuationRecordNumber;
	char LocatorFrequency[5];
	char RunwayIdentifier[5];
	char MarkerLatitude[9];
	char MarkerLongitude[10];
	char MinorAxisBearing[4];
	char LocatorLatitude[9];
	char LocatorLongitude[10];
	char LocatorClass[5];
	char LocatorFacilityCharacteristics[5];
	char LocatorIdentifier[4];
	char Blank3[2];
	char MagneticVariation[5];
	char Blank4[2];
	char FacilityElevation[5];
	char Reserved[21];
	char FileRecordNo[5];
	char CycleDate[4];
};


enum NavAIDType {
	DME,
	NDB,
	TACAN,
	VOR,
	VORD, // VORDME
	VORT, // VORTACAN
	LOC,
	GS,
	ILS,
	ILSD,
	GLS,
	OuterMarker,
	MiddleMarker,
	InnerMarker,
	BackMarker,
	UnknownNavAID
};

enum ILSCategory {
	ILSNoGS,
	ILSCat1,
	ILSCat2,
	ILSCat3,
	IGS,
	LDA,
	LDAGS,
	SDF,
	SDFGS,
	NoILS
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct A424Header {
	std::string cycle;        // e.g. "2603"
	std::string created;      // e.g. "02-MAR-2026"
	std::string supplier;     // e.g. "JEPPESEN"
	bool valid = false;
};

class A424Parser
{
public:
	A424Parser();
	virtual ~A424Parser();

	static bool ParseA424(std::string filename, AS::World* pWorld);
	static A424Header ReadHeader(const std::string& filename);

	static bool ParseVHFRecord(char* record, AS::World* pWorld);
	static bool ParseNDBRecord(char* record, AS::World* pWorld);
	static bool ParseLOCGSRecord(char* record, AS::World* pWorld);
	static bool ParseLOCMarkerRecord(char* record, AS::World* pWorld);

	static int GetSubstrInt(char* buf, size_t len);
	static float GetSubstrFloat(char* buf, size_t len);
	static double GetSubstrDouble(char* buf, size_t len);
	static std::string GetSubstrString(char* buf, size_t len);

	static void GetLatitudeLongitudeFromString(char* data, double& latitude, double& longitude);
	static void GetLatitudeLongitudeFromStringHP(char* data, double& latitude, double& longitude);

	static NavAIDType GetNavAidTypeFromClass(std::string navaidClass, char& flag, char& ndb_flag);
	static ILSCategory GetILSCategory(char code);
	static std::pair<std::string, std::string> GetNameAndCity(std::string ap_city);
};

} // A424