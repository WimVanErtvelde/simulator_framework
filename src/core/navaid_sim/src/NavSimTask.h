#pragma once

#include "Model.h"
#include "World.h"
#include "WorldParser.h"
#include "TerrainModel.h"
#include "LOSChecker.h"
#include "VOR_Receiver.h"
#include "NDB_Receiver.h"
#include "ILS_LocalizerReceiver.h"
#include "ILS_GlideslopeReceiver.h"
#include "ILS_MarkerReceiver.h"
#include "DME_Receiver.h"

// ---------------------------------------------------------------------------
// NavSimTask — drives one simulation step (no DSim dependency)
// ---------------------------------------------------------------------------
// Usage:
//   AS::World  world;
//   AS::Model  model;
//   NavSimTask task(&world, &model);
//
//   model.setPosition(51.5f, 4.2f, 8000.f);
//   model.setFrequency(1, 11580);   // 115.80 MHz on NAV1
//   task.step();
//   AS::RadioResult r = model.getRadioResult(1);
// ---------------------------------------------------------------------------
class NavSimTask
{
public:
    // tileDir: path to folder containing *.hgt terrain files.
    //          Pass empty string to disable terrain LOS checks.
    NavSimTask(AS::World* world, AS::Model* model,
               const std::string& tileDir = "../terrain/srtm3/");
    ~NavSimTask() = default;

    // Run one simulation cycle.
    void step();

    // Expose the LOS checker for ad-hoc queries from calling code.
    LOSChecker& getLOSChecker() { return los; }

    // Expose terrain model for elevation queries.
    TerrainModel& getTerrainModel() { return terrain; }

private:
    AS::Model* model = nullptr;

    TerrainModel terrain;
    LOSChecker   los;

    AS::VOR_Receiver           vor;
    AS::NDB_Receiver           ndb;   // intentionally no LOS (ground wave)
    AS::ILS_LocalizerReceiver  loc;
    AS::ILS_GlideslopeReceiver gs;
    AS::ILS_MarkerReceiver     marker;
    AS::DME_Receiver           dme;
};
