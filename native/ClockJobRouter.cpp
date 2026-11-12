#include "ClockJobRouter.h"
#include <new>

namespace tfu {
struct ClockJobRouter::Context {
  ClockJobRouter *router;ClockDomainGate::Scope scope;ClockDomainGate::Job parent;
};
struct ClockJobRouter::Envelope {
  ClockJobRouter *router=nullptr;Callback callback=nullptr;void *state=nullptr;
  ClockDomainGate::Scope scope;ClockDomainGate::Job job;
  volatile LONG references=2; // Submit call plus exactly one native callback.
};
__declspec(thread) ClockJobRouter::Context *ClockJobRouter::current_=nullptr;
ClockJobRouter *ClockJobRouter::CurrentRouter(){return current_?current_->router:nullptr;}
void ClockJobRouter::EnterCallback(){Lock lock(&mutex_);++callbacks_;}
void ClockJobRouter::LeaveCallback(){Lock lock(&mutex_);--callbacks_;}
bool ClockJobRouter::BeginRoot(){Lock lock(&mutex_);if(!accepting_||roots_==UINT32_MAX)return false;++roots_;return true;}
void ClockJobRouter::EndRoot(){Lock lock(&mutex_);--roots_;}
bool ClockJobRouter::BeginSubmission(bool descendant){
  Lock lock(&mutex_);if((!accepting_&&!descendant)||envelopes_==UINT32_MAX||submitters_==UINT32_MAX)return false;
  ++envelopes_;++submitters_;return true;
}
void ClockJobRouter::RejectSubmission(Envelope *job){
  delete job;Lock lock(&mutex_);--envelopes_;--submitters_;
}
void ClockJobRouter::MarkUncertain(){Lock lock(&mutex_);uncertain_=true;}
void ClockJobRouter::ReleaseEnvelope(Envelope *job){
  if(InterlockedDecrement(&job->references)!=0)return;
  auto *router=job->router;delete job;
  Lock lock(&router->mutex_);--router->envelopes_; // Last router-storage access.
}
void ClockJobRouter::FinishSubmission(Envelope *job,bool abnormal){
  {Lock lock(&mutex_);if(abnormal)uncertain_=true;--submitters_;}
  // On unknown failure keep the callback reference alive. The original may
  // already have queued it even when no native handle was returned.
  ReleaseEnvelope(job);
}
void __cdecl ClockJobRouter::RoutedBody(void *opaque,int argument){
  auto *job=static_cast<Envelope *>(opaque);auto *router=job->router;
  Context context={router,job->scope,job->job};auto *previous=current_;current_=&context;
  router->EnterCallback();
  __try {job->callback(job->state,argument);}
  __finally {current_=previous;router->LeaveCallback();}
}
void __cdecl ClockJobRouter::NativeCallback(void *opaque,int argument){
  auto *job=static_cast<Envelope *>(opaque);
  __try {
    if(!job->router->gate_.Invoke(job->job,RoutedBody,job,argument)){
      job->router->MarkUncertain();RaiseException(0xE0424270,0,0,nullptr);
    }
  }__finally {ReleaseEnvelope(job);}
}
ClockJobRouter::Envelope *ClockJobRouter::RetainEnvelope(Callback callback,void *state){
  auto *job=new(std::nothrow) Envelope;if(!job)return nullptr;
  auto *parent=current_&&current_->router==this?current_:nullptr;
  if(!BeginSubmission(parent&&parent->parent.id)){delete job;return nullptr;}
  job->router=this;job->callback=callback;job->state=state;
  bool retained=false;
  if(parent&&parent->scope.id){job->scope=parent->scope;retained=gate_.RetainChild(parent->scope,parent->parent.id?&parent->parent:nullptr,&job->job);}
  else retained=gate_.RetainForeign(parent?&parent->parent:nullptr,&job->job);
  if(!retained){RejectSubmission(job);return nullptr;}
  return job;
}
bool ClockJobRouter::Submit(NativeSubmit original,Callback callback,void *state,int argument,uint32_t label,uint32_t *result){
  if(!original||!callback||!result)return false;
  auto *job=RetainEnvelope(callback,state);if(!job)return false;
  uint32_t nativeResult=0;
  __try {nativeResult=original(NativeCallback,job,argument,label);}
  __finally {FinishSubmission(job,AbnormalTermination()!=0);}
  *result=nativeResult;return true;
}
bool ClockJobRouter::SubmitChecked(CheckedSubmit original,Callback callback,void *state,int argument,uint32_t label,uint32_t *result){
  if(!original||!callback||!result)return false;
  auto *job=RetainEnvelope(callback,state);if(!job)return false;
  uint32_t nativeResult=0;bool published=false;
  __try {
    published=original(NativeCallback,job,argument,label,&nativeResult);
    if(!published){
      if(gate_.CancelUnpublished(job->job))ReleaseEnvelope(job); // Unpublished callback reference.
      else MarkUncertain(); // Rejection contract could not be confirmed.
    }
  }
  __finally {FinishSubmission(job,AbnormalTermination()!=0);}
  if(published)*result=nativeResult;
  return published;
}
bool ClockJobRouter::WithScope(ClockDomainGate::Scope scope,Callback body,void *context,int argument){
  if(!body||!gate_.OwnsScope(scope))return false;
  if(current_&&current_->router==this&&current_->parent.id)return false;
  if(!BeginRoot())return false;
  Context active={this,scope,{}};auto *previous=current_;current_=&active;
  __try {body(context,argument);}
  __finally {if(AbnormalTermination())gate_.MarkScopeFailed(scope);current_=previous;EndRoot();}
  return true;
}
void ClockJobRouter::Close(){Lock lock(&mutex_);accepting_=false;}
ClockJobRouter::Snapshot ClockJobRouter::Read(){Lock lock(&mutex_);return {envelopes_,submitters_,callbacks_,roots_,accepting_,uncertain_};}
bool ClockJobRouter::CanDetach(){Lock lock(&mutex_);return !accepting_&&!envelopes_&&!submitters_&&!callbacks_&&!roots_&&!uncertain_;}
bool ClockJobRouter::ReconcileAfterNativeQuiescence(){Lock lock(&mutex_);if(accepting_||envelopes_||submitters_||callbacks_||roots_)return false;uncertain_=false;return true;}
}
