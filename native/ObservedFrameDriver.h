#pragma once
#include "FixedStepDriver.h"
#include "TimerObservation.h"

namespace tfu {
// Owns accepted-frame admission. An unsupported accepted native call is a
// persistent boundary: the caller must reconcile it and outstanding work/time
// before beginning a fresh epoch. Never fall back after consuming a half frame.
class ObservedFrameDriver {
public:
  enum class Boundary:DWORD {None,InvalidObservation,SingleStep,NativeMode,SourceChanged,NativeFailure};
  struct ExecutionState {DWORD mode;float scale,cap;};
private:
  FixedStepDriver driver_;
  DWORD epoch_=0;
  float cap_=0;
  Boundary boundary_=Boundary::None;
  TimerObservation pending_={};
  TimerObservation admitted_={};
  ExecutionState executionBoundary_={};
  bool executionBoundaryValid_=false;
  bool executing_=false;
  int Hold(const TimerObservation &observation,Boundary reason){pending_=observation;boundary_=reason;return -3;}
public:
  const FixedStepDriver &Driver()const{return driver_;}
  Boundary Reason()const{return boundary_;}
  const TimerObservation &PendingObservation()const{return pending_;}
  const ExecutionState &ExecutionBoundary()const{return executionBoundary_;}
  bool HasExecutionBoundary()const{return executionBoundaryValid_;}
  bool BeginAfterNativeReconcile(DWORD epoch,float elapsed){
    if(executing_||!epoch||epoch<=epoch_||(driver_.Open()&&!driver_.Faulted()&&boundary_==Boundary::None))return false;
    if(!driver_.ResetAfterNativeReconcile(elapsed))return false;
    epoch_=epoch;cap_=0;boundary_=Boundary::None;pending_={};admitted_={};executionBoundary_={};executionBoundaryValid_=false;return true;
  }
  int Prepare(const TimerObservation &observation,DWORD generation){
    if(boundary_!=Boundary::None)return -3;
    if(executing_||!epoch_||driver_.Open()||driver_.Faulted())return -2;
    if(observation.accepted>1)return Hold(observation,Boundary::InvalidObservation);
    if(!observation.accepted)return 0;
    if(cap_&&observation.cap!=cap_)return Hold(observation,Boundary::SourceChanged);
    if(observation.entryMode==3&&observation.exitMode==2)return Hold(observation,Boundary::SingleStep);
    if(observation.entryMode!=observation.exitMode)return Hold(observation,Boundary::SourceChanged);
    if(observation.entryMode>2)return Hold(observation,Boundary::NativeMode);
    if((observation.cap!=30.f&&observation.cap!=60.f)||
       !std::isfinite(observation.entryScale)||observation.entryScale<0||
       observation.entryScale!=observation.exitScale||
       !std::isfinite(observation.realDt)||observation.realDt<0||
       !std::isfinite(observation.gameplayDt)||observation.gameplayDt<0)
      return Hold(observation,Boundary::InvalidObservation);
    const bool paused=observation.gameplayDt==0;
    if(observation.entryMode&& !paused)return Hold(observation,Boundary::InvalidObservation);
    if(!observation.entryMode){
      // The native capped reference is unscaled. Reconstruct the scaled
      // cadence reference explicitly; do not overwrite the observed value.
      if(!std::isfinite(observation.reference)||observation.reference<=0||
         observation.gameplayDt!=static_cast<float>(static_cast<double>(observation.reference)*observation.entryScale))
        return Hold(observation,Boundary::InvalidObservation);
    }
    PresentationFrameCapture frame={generation,observation.cap,observation.gameplayDt,
      observation.gameplayDt*(observation.cap==60.f?2.f:1.f),paused?0u:1u,
      observation.reference,observation.exitMode,epoch_};
    const int status=driver_.Prepare(frame,true);
    if(status>0){cap_=observation.cap;admitted_=observation;}
    return status<0?Hold(observation,Boundary::SourceChanged):status;
  }
  // Re-read these cells at the simulation fence under the same writer/job
  // exclusion. Native input can change mode after the timer returned.
  bool Run(const ExecutionState &current,FixedStepDriver::Update update,void *context){
    if(executing_||boundary_!=Boundary::None||!driver_.Open()||driver_.Faulted())return false;
    if(current.mode!=admitted_.exitMode||current.scale!=admitted_.exitScale||current.cap!=admitted_.cap){
      executionBoundary_=current;executionBoundaryValid_=true;Hold(admitted_,Boundary::SourceChanged);return false;
    }
    executing_=true;bool result=false;
    __try {result=driver_.Run(update,context);}
    __finally {executing_=false;if(driver_.Faulted())Hold(admitted_,Boundary::NativeFailure);}
    return result;
  }
  bool Finish(){return boundary_==Boundary::None&&driver_.Finish();}
};
}
