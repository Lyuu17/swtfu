#pragma once
#include "ClockDomainGate.h"

namespace tfu {
// LEC-compatible exactly-once callback contract. Storage outlives every native
// submitter/callback. Stop callers and join native jobs before module unload;
// counters alone do not prove callback machine-code epilogues have returned.
class ClockJobRouter {
public:
  using Callback=ClockDomainGate::Callback;
  using NativeSubmit=uint32_t (__cdecl *)(Callback,void *,int,uint32_t);
  // False MUST prove no callback was invoked/queued/published, not merely a
  // failed native handle. Exceptions retain unknown work as with NativeSubmit.
  using CheckedSubmit=bool (__cdecl *)(Callback,void *,int,uint32_t,uint32_t *);
  struct Snapshot {uint32_t envelopes,submitters,callbackContexts,rootContexts;bool accepting,uncertain;};
private:
  struct Context;
  struct Envelope;
  static __declspec(thread) Context *current_;
  ClockDomainGate &gate_;
  SRWLOCK mutex_=SRWLOCK_INIT;
  struct Lock {SRWLOCK *mutex;explicit Lock(SRWLOCK *p):mutex(p){AcquireSRWLockExclusive(mutex);}~Lock(){ReleaseSRWLockExclusive(mutex);}};
  uint32_t envelopes_=0,submitters_=0,callbacks_=0,roots_=0;
  bool accepting_=true,uncertain_=false;
  bool BeginSubmission(bool descendant);
  void EnterCallback();
  void LeaveCallback();
  bool BeginRoot();
  void EndRoot();
  void RejectSubmission(Envelope *job);
  Envelope *RetainEnvelope(Callback callback,void *state);
  void FinishSubmission(Envelope *job,bool abnormal);
  void MarkUncertain();
  static void ReleaseEnvelope(Envelope *job);
  static void __cdecl RoutedBody(void *opaque,int argument);
  static void __cdecl NativeCallback(void *opaque,int argument);
public:
  explicit ClockJobRouter(ClockDomainGate &gate):gate_(gate){}
  ClockJobRouter(const ClockJobRouter &)=delete;
  ClockJobRouter &operator=(const ClockJobRouter &)=delete;
  // False leaves result untouched and DOES NOT call the original submitter.
  // Caller must defer/fail admission; never pretend handle0 means work ran.
  bool Submit(NativeSubmit original,Callback callback,void *state,int argument,uint32_t label,uint32_t *result);
  bool SubmitChecked(CheckedSubmit original,Callback callback,void *state,int argument,uint32_t label,uint32_t *result);
  bool WithScope(ClockDomainGate::Scope scope,Callback body,void *context,int argument);
  void Close(); // New roots stop; admitted callbacks may retain descendants.
  Snapshot Read();
  bool CanDetach();
  bool HasCurrentContext()const{return current_&&CurrentRouter()==this;}
  // Sticky uncertainty only clears after native scheduler/state reconciliation.
  // Unexecuted callback references cannot be guessed cancelled or freed here.
  bool ReconcileAfterNativeQuiescence();
private:
  static ClockJobRouter *CurrentRouter();
};
}
