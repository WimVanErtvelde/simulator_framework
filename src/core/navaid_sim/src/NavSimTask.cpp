#include "NavSimTask.h"

NavSimTask::NavSimTask(AS::World* pWorld, AS::Model* pModel,
                       const std::string& tileDir)
    : model(pModel),
      terrain(tileDir),
      los(&terrain),
      vor(pWorld, pModel),
      ndb(pWorld, pModel),
      loc(pWorld, pModel),
      gs(pWorld, pModel),
      marker(pWorld, pModel),
      dme(pWorld, pModel)
{
    // Wire LOS checker to all VHF/UHF receivers.
    // NDB is intentionally excluded — it uses LF/MF ground-wave propagation
    // and does not require strict terrain line-of-sight.
    vor.setLOSChecker(&los);
    loc.setLOSChecker(&los);
    gs.setLOSChecker(&los);
    dme.setLOSChecker(&los);
}

void NavSimTask::step()
{
    vor.update();
    ndb.update();
    loc.update();
    gs.update();
    dme.update();
    marker.updateOnce();
}
