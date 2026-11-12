#pragma once
#include <windows.h>
#include <cstdint>
#include <limits>
#include <map>

namespace tfu {
// Retain every queued clock-reading callback BEFORE submitting it, and propagate
// its job token to descendants. Gate storage and native clock storage outlive
// all tokens. This does not discover readers, install hooks, or own the clock.
// Admission failure forbids unprotected execution; the caller must defer work.
class ClockDomainGate {
public:
  struct Job {uint64_t epoch=0,id=0;};
  struct Scope {uint64_t epoch=0,id=0;};
  struct Snapshot {unsigned phase,foreignJobs,childJobs,runningJobs;bool failed,quarantined;};
  using Callback=void (__cdecl *)(void *,int);
private:
  struct Record {uint64_t scope=0;DWORD thread=0,creator=0;}; // thread0 means queued.
  SRWLOCK mutex_=SRWLOCK_INIT;
  CONDITION_VARIABLE changed_=CONDITION_VARIABLE_INIT;
  struct Lock {SRWLOCK *mutex;explicit Lock(SRWLOCK *p):mutex(p){AcquireSRWLockExclusive(mutex);}~Lock(){ReleaseSRWLockExclusive(mutex);}};
  std::map<uint64_t,Record> jobs_;
  uint64_t epoch_=1,serial_=0,scope_=0;
  unsigned phase_=0; //0 baseline,1 draining existing readers,2 simulation.
  DWORD owner_=0;
  bool draining_=false,failed_=false,quarantined_=false;
  bool Valid(Scope scope)const{return phase_==2&&scope.epoch==epoch_&&scope.id&&scope.id==scope_;}
  auto Find(Job job)->std::map<uint64_t,Record>::iterator {return job.epoch==epoch_&&job.id?jobs_.find(job.id):jobs_.end();}
  bool Add(uint64_t scope,Job *output){
    if(!epoch_||serial_==(std::numeric_limits<uint64_t>::max)())return false;
    const uint64_t id=serial_+1;jobs_.emplace(id,Record{scope,0,GetCurrentThreadId()});serial_=id;*output={epoch_,id};return true;
  }
  bool OnThisThread()const{for(const auto &entry:jobs_)if(entry.second.thread==GetCurrentThreadId())return true;return false;}
  bool Wait(DWORD timeout,ULONGLONG started){
    const ULONGLONG elapsed=GetTickCount64()-started;
    if(timeout!=INFINITE&&elapsed>=timeout)return false;
    return SleepConditionVariableSRW(&changed_,&mutex_,timeout==INFINITE?INFINITE:timeout-static_cast<DWORD>(elapsed),0)!=0;
  }
  bool Start(Job job){
    Lock lock(&mutex_);auto found=Find(job);if(found==jobs_.end()||found->second.thread)return false;
    if(found->second.scope?(phase_!=2||found->second.scope!=scope_):phase_==2)return false;
    found->second.thread=GetCurrentThreadId();return true;
  }
  void Complete(Job job,bool success){
    Lock lock(&mutex_);auto found=Find(job);
    if(found==jobs_.end()||found->second.thread!=GetCurrentThreadId())return;
    if(!success){if(found->second.scope)failed_=true;else quarantined_=true;}
    jobs_.erase(found);WakeAllConditionVariable(&changed_);
  }
public:
  explicit ClockDomainGate(uint64_t epoch=1):epoch_(epoch){}
  ClockDomainGate(const ClockDomainGate &)=delete;
  ClockDomainGate &operator=(const ClockDomainGate &)=delete;
  bool OwnsScope(Scope scope){Lock lock(&mutex_);return Valid(scope)&&owner_==GetCurrentThreadId();}
  bool MarkScopeFailed(Scope scope){Lock lock(&mutex_);if(!Valid(scope)||owner_!=GetCurrentThreadId())return false;failed_=true;return true;}
  bool RetainForeign(const Job *parent,Job *output){
    if(!output)return false;*output={};
    try {Lock lock(&mutex_);
      if(parent){auto found=Find(*parent);if(found==jobs_.end()||found->second.scope||found->second.thread!=GetCurrentThreadId()||phase_==2)return false;}
      else if(phase_!=0)return false;
      return Add(0,output);
    }catch(...){return false;}
  }
  bool Acquire(DWORD timeout,Scope *output){
    if(!output)return false;*output={};Lock lock(&mutex_);
    if(!epoch_||phase_||quarantined_||OnThisThread()||serial_==(std::numeric_limits<uint64_t>::max)())return false;
    phase_=1;owner_=GetCurrentThreadId();const ULONGLONG started=GetTickCount64();
    while(!jobs_.empty())if(!Wait(timeout,started)&&!jobs_.empty()){
      phase_=0;owner_=0;WakeAllConditionVariable(&changed_);return false;
    }
    if(quarantined_){phase_=0;owner_=0;WakeAllConditionVariable(&changed_);return false;}
    phase_=2;scope_=++serial_;draining_=false;failed_=false;*output={epoch_,scope_};return true;
  }
  bool RetainChild(Scope scope,const Job *parent,Job *output){
    if(!output)return false;*output={};
    try {Lock lock(&mutex_);if(!Valid(scope))return false;
      if(parent){auto found=Find(*parent);if(found==jobs_.end()||found->second.scope!=scope_||found->second.thread!=GetCurrentThreadId())return false;}
      else if(owner_!=GetCurrentThreadId()||draining_)return false;
      return Add(scope_,output);
    }catch(...){return false;}
  }
  bool Invoke(Job job,Callback callback,void *context,int argument){
    if(!callback||!Start(job))return false;bool success=false;
    __try {callback(context,argument);success=true;}
    __finally {Complete(job,success);}
    return true;
  }
  // Only for an adapter-confirmed rejection BEFORE any native publication or
  // callback invocation. Not a cancellation API for published/unknown work.
  bool CancelUnpublished(Job job){
    Lock lock(&mutex_);auto found=Find(job);
    if(found==jobs_.end()||found->second.thread||found->second.creator!=GetCurrentThreadId())return false;
    if(found->second.scope?(phase_!=2||found->second.scope!=scope_):phase_==2)return false;
    jobs_.erase(found);WakeAllConditionVariable(&changed_);return true;
  }
  bool Drain(Scope scope,DWORD timeout){
    Lock lock(&mutex_);if(!Valid(scope)||owner_!=GetCurrentThreadId()||OnThisThread())return false;
    draining_=true;const ULONGLONG started=GetTickCount64();
    while(!jobs_.empty())if(!Wait(timeout,started)&&!jobs_.empty())return false;
    return true; // Scope remains exclusive until caller restores native clocks.
  }
  // Restore the original native clocks BEFORE release, after successful drain.
  // A timeout never permits release while queued/running children remain.
  bool ReleaseAfterClockRestore(Scope scope){
    Lock lock(&mutex_);if(!Valid(scope)||owner_!=GetCurrentThreadId()||!draining_||!jobs_.empty())return false;
    quarantined_=quarantined_||failed_;phase_=0;owner_=0;scope_=0;draining_=false;WakeAllConditionVariable(&changed_);return true;
  }
  Snapshot Read(){
    Lock lock(&mutex_);Snapshot result={phase_,0,0,0,failed_,quarantined_};
    for(const auto &entry:jobs_){if(entry.second.scope)++result.childJobs;else ++result.foreignJobs;if(entry.second.thread)++result.runningJobs;}
    return result;
  }
  // Explicit caller reconciliation of native partial state, time and histories.
  bool ResetAfterNativeReconcile(uint64_t nextEpoch=0){
    Lock lock(&mutex_);if(phase_||!jobs_.empty()||epoch_==(std::numeric_limits<uint64_t>::max)())return false;
    if(!nextEpoch)nextEpoch=epoch_+1;
    if(nextEpoch<=epoch_)return false;
    epoch_=nextEpoch;serial_=0;failed_=false;quarantined_=false;return true;
  }
};
}
