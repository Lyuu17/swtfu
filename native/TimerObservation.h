#pragma once
#include <windows.h>

namespace tfu {
// Single timer owner, with external exclusion of all writers. This records the
// native call; it does not infer accepted simulation work or install a hook.
struct TimerObservation {
  DWORD entryMode,exitMode,accepted;
  float cap,entryScale,exitScale,gameplayDt,realDt,reference;
};
using TimerFunction=BYTE (__cdecl *)(void *,float,void *,void *,float *);
inline BYTE ObserveTimer(TimerFunction original,void *clock,float cap,
                         void *previous,void *accepted,float *reference,
                         const volatile DWORD *mode,const volatile float *scale,
                         const volatile float *dt,const volatile float *realDt,
                         TimerObservation *output) {
  TimerObservation next={};
  next.entryMode=*mode;next.entryScale=*scale;next.cap=cap;
  // Publish only after native success. A fault may have changed native state;
  // the caller must quarantine/reconcile it, never replay this observation.
  const BYTE result=original(clock,cap,previous,accepted,reference);
  next.exitMode=*mode;next.exitScale=*scale;next.accepted=result;
  next.gameplayDt=*dt;next.realDt=*realDt;next.reference=*reference;
  *output=next;return result;
}
static_assert(sizeof(TimerObservation)==36,"Timer observation ABI");
}
