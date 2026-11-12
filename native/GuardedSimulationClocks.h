#pragma once
#include <windows.h>
#include "SimulationClocks.h"
namespace tfu {
// Explicit lifetime: this object, its gate and clock storage outlive every
// admitted reader. No destructor restores clocks or releases an active scope.
// The gate must cover ALL readers/writers; this class cannot discover them.
template<class Gate> class GuardedSimulationClocks {
  Gate &gate_;
  SimulationClockCells cells_;
  SimulationClockSnapshot saved_={};
  typename Gate::Scope scope_={};
  DWORD owner_=0;
  bool active_=false,savedValid_=false,restored_=false,faulted_=false;
public:
  GuardedSimulationClocks(Gate &gate,SimulationClockCells clockStorage):gate_(gate),cells_(clockStorage){}
  GuardedSimulationClocks(const GuardedSimulationClocks &)=delete;
  GuardedSimulationClocks &operator=(const GuardedSimulationClocks &)=delete;
  bool Active()const{return active_;}
  bool Faulted()const{return faulted_;}
  typename Gate::Scope Token()const{return scope_;}
  bool AcquireAndPublish(DWORD timeout,float interval,float elapsed,bool publishTarget){
    if(active_||faulted_||!std::isfinite(interval)||interval<0||!std::isfinite(elapsed))return false;
    typename Gate::Scope token={};if(!gate_.Acquire(timeout,&token))return false;
    scope_=token;owner_=GetCurrentThreadId();active_=true;savedValid_=false;restored_=false;
    bool success=false;
    __try {
      CaptureSimulationClocks(cells_,&saved_);savedValid_=true;
      success=PublishSimulationClocks(cells_,interval,elapsed,publishTarget);
    }
    __finally {if(!success)faulted_=true;}
    return success;
  }
  // Timeout/wrong-thread calls leave every clock and the exclusive scope in
  // place. A failed restore keeps admission closed and propagates its fault.
  bool RestoreAndRelease(DWORD timeout){
    if(!active_||owner_!=GetCurrentThreadId()||!savedValid_)return false;
    if(!gate_.Drain(scope_,timeout))return false;
    if(!restored_){
      bool success=false;
      __try {RestoreSimulationClocks(cells_,saved_);restored_=true;success=true;}
      __finally {if(!success)faulted_=true;}
    }
    if(!gate_.ReleaseAfterClockRestore(scope_))return false;
    active_=false;owner_=0;scope_={};return true;
  }
  // Caller must reconcile native partial state/time/history and the backend.
  // A capture fault without a valid snapshot deliberately cannot auto-release.
  bool ResetAfterNativeReconcile(){if(active_)return false;faulted_=false;return true;}
};
}
