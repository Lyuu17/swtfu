#pragma once
#include <cmath>
#include <cstring>
namespace tfu {
// Caller owns writer/job exclusion for the entire snapshot/publish/drain/restore
// interval. Storage must outlive all readers. No synchronization or hooks here.
struct SimulationClockCells {float *interval,*elapsed,*target,*packedInterval,*packedInverse;};
struct SimulationClockSnapshot {float interval,elapsed,target,packedInterval[4],packedInverse[4];};
inline void CaptureSimulationClocks(const SimulationClockCells &c,SimulationClockSnapshot *s){
  std::memcpy(&s->interval,c.interval,4);std::memcpy(&s->elapsed,c.elapsed,4);std::memcpy(&s->target,c.target,4);
  std::memcpy(s->packedInterval,c.packedInterval,16);std::memcpy(s->packedInverse,c.packedInverse,16);
}
inline bool PublishSimulationClocks(const SimulationClockCells &c,float interval,float elapsed,bool publishTarget){
  if(!std::isfinite(interval)||interval<0||!std::isfinite(elapsed))return false;
  // Original41625C..4162D3 uses double division then float conversion, NOT
  // the approximate RCPSS used to select the capped timer interval.
  const float inverse=interval>0?static_cast<float>(1.0/static_cast<double>(interval)):1.f;
  *c.interval=interval;*c.elapsed=elapsed;
  if(publishTarget)*c.target=interval<0.06666667014360428f?interval:0.06666667014360428f;
  for(unsigned i=0;i<4;++i){c.packedInterval[i]=interval;c.packedInverse[i]=inverse;}
  return true;
}
inline void RestoreSimulationClocks(const SimulationClockCells &c,const SimulationClockSnapshot &s){
  std::memcpy(c.interval,&s.interval,4);std::memcpy(c.elapsed,&s.elapsed,4);std::memcpy(c.target,&s.target,4);
  std::memcpy(c.packedInterval,s.packedInterval,16);std::memcpy(c.packedInverse,s.packedInverse,16);
}
static_assert(sizeof(SimulationClockSnapshot)==44,"Clock snapshot ABI");
}
