#pragma once
#include <windows.h>
#include "../analysis/presentation_cadence.h"

namespace tfu {
// Single owner/thread. The caller supplies a validated native timer observation,
// fences every simulation/shared-clock reader, and restores clock/render scopes.
// This component neither installs hooks nor changes shared engine clocks.
class FixedStepDriver {
public:
  using Update=void (__cdecl *)(void *,float,unsigned long long);
private:
  PresentationCadenceState completed_={},candidate_={};
  PresentationFramePlan plan_={};
  int status_=0;
  bool open_=false,ran_=false,running_=false,faulted_=false;
  float simulationElapsed_=0,preparedElapsed_=0;
public:
  const PresentationCadenceState &Completed()const{return completed_;}
  const PresentationFramePlan &Plan()const{return plan_;}
  bool Open()const{return open_;}
  bool Faulted()const{return faulted_;}
  float SimulationElapsed()const{return simulationElapsed_;}
  float PreparedElapsed()const{return preparedElapsed_;}
  float FrameElapsed()const{if(faulted_)return simulationElapsed_;const float elapsed=open_&&plan_.step?preparedElapsed_:simulationElapsed_;const double carry=open_?candidate_.carry:completed_.carry;return static_cast<float>(static_cast<double>(elapsed)+carry);}
  // Match original426930: double addition followed by a float store once per
  // simulation step, not per presentation frame or by casting total doubles.
  bool Reset(float initialElapsed=0){if(open_||running_||faulted_||!std::isfinite(initialElapsed))return false;completed_={};candidate_={};plan_={};status_=0;ran_=false;simulationElapsed_=preparedElapsed_=initialElapsed;return true;}
  int Prepare(const PresentationFrameCapture &frame,bool accepted){
    if(open_||running_||faulted_)return -2;
    candidate_=completed_;PresentationFramePlan next={};
    const int status=PlanPresentationFrame(&candidate_,&frame,accepted,&next);
    plan_=next;status_=status;
    preparedElapsed_=simulationElapsed_;
    if(status<=0)return status;
    if(plan_.step){preparedElapsed_=static_cast<float>(static_cast<double>(simulationElapsed_)+static_cast<double>(plan_.stepDt));if(!std::isfinite(preparedElapsed_)){candidate_=completed_;plan_={};preparedElapsed_=simulationElapsed_;status_=-1;return status_;}}
    open_=true;ran_=false;
    // Frames with no update consume only accepted time/carry. Positive steps
    // and paused zero-dt maintenance commit only after the callback returns.
    if(!plan_.step&&status_!=2){completed_=candidate_;ran_=true;}
    return status;
  }
  bool Run(Update update,void *context){
    if(!open_||ran_||running_||faulted_||!update)return false;
    running_=true;bool success=false;
    __try {update(context,plan_.stepDt,plan_.step?candidate_.steps:0);success=true;}
    __finally {
      running_=false;
      if(success){completed_=candidate_;simulationElapsed_=preparedElapsed_;ran_=true;}
      else faulted_=true; // Native state may have changed. Never retry it.
    }
    return true;
  }
  bool Finish(){if(!open_||!ran_||running_||faulted_)return false;open_=false;return true;}
  // Only after the caller reconciles pending accepted time AND native partial
  // state, retires old presentation histories, and supplies a fresh timer epoch.
  bool ResetAfterNativeReconcile(float initialElapsed=0){if(running_||!std::isfinite(initialElapsed))return false;open_=false;faulted_=false;return Reset(initialElapsed);}
};
}
