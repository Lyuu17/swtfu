#define NOMINMAX
#include "TFUPoseHistory.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include "../analysis/pose_history_registry.h"
#include "WorldPresentation.h"
#include "CameraHistoryRegistry.h"
#include "ClockJobRouter.h"
static_assert(sizeof(void *)==4,"Build this module for x86");
namespace {
SRWLOCK moduleMutex=SRWLOCK_INIT;
struct Lock {Lock(){AcquireSRWLockExclusive(&moduleMutex);}~Lock(){ReleaseSRWLockExclusive(&moduleMutex);}};
std::unique_ptr<PoseHistoryRegistry> registry;
std::unique_ptr<tfu::CameraHistoryRegistry> cameras;
std::unique_ptr<tfu::ClockDomainGate> clockGate;
std::unique_ptr<tfu::ClockJobRouter> clockRouter;
TFUHistoryNativeClockSubmit nativeClockSubmit=nullptr;
uint64_t clockEpoch=0;
ULONG activeClockInvocations=0;
uint32_t session=0;
bool closing=false;
CONDITION_VARIABLE invocationIdle=CONDITION_VARIABLE_INIT;
ULONG activeInvocations=0;
struct UploadScope {const float *data;uint32_t first,count;bool consumed;};
static __declspec(thread) UploadScope *uploadScope;
static __declspec(thread) ULONG invocationDepth;
TFUHistoryNativeInitialize ownerInitialize=nullptr;
TFUHistoryNativeRelease ownerRelease=nullptr;
TFUHistoryNativeBind geometryBind=nullptr;
TFUHistoryNativeBatch animationBatch=nullptr;
TFUHistoryNativeRelease geometryCleanup=nullptr;
uint64_t capturePass=0;
TFUHistoryNativeDraw nativeDraw=nullptr;
TFUHistoryNativeWorldDraw nativeWorldDraw=nullptr;
TFUHistoryNativeWorldPublish nativeWorldPublish=nullptr;
TFUHistoryNativeWorldAssign nativeWorldAssign=nullptr;
TFUHistoryNativeWorldClear nativeWorldClear=nullptr;
TFUHistoryNativeWorldCreate nativeWorldCreate=nullptr;
TFUHistoryNativeWorldClear nativeWorldDestroy=nullptr;
TFUHistoryNativePrepare nativeWorldPrepare=nullptr;
TFUHistoryNativeCameraSetter nativeCameraSetter=nullptr;
static __declspec(thread) bool cameraCopyActive;
TFUHistoryNativeCameraRoutine nativeCameraCreate=nullptr,nativeCameraDestroy=nullptr,nativeCameraUpdate=nullptr;
static __declspec(thread) uint64_t cameraCaptureStep;
static __declspec(thread) uint32_t cameraCaptureSession,cameraCaptureCut;
static __declspec(thread) ULONG cameraUpdateDepth;
static __declspec(thread) ULONG preparationDepth;
ULONG activePreparations=0;
uint64_t preparationStep=0;
uint32_t preparationSession=0;
bool preparationFailed=false;
static __declspec(thread) uint64_t worldCaptureStep;
static __declspec(thread) uint32_t worldCaptureSession;
static __declspec(thread) bool worldDrawActive;
static __declspec(thread) bool *worldNestedFlag;
TFUHistoryBasisBuilder nativeBasis=nullptr;
static __declspec(thread) uint32_t renderSession;
static __declspec(thread) float renderAlpha;
static __declspec(thread) bool renderEnabled;
bool EnterInvocation(bool *available){Lock lock;if(activeInvocations==ULONG_MAX)return false;++activeInvocations;++invocationDepth;*available=registry&&!closing;return true;}
void LeaveInvocation(){Lock lock;--invocationDepth;--activeInvocations;if(!activeInvocations)WakeAllConditionVariable(&invocationIdle);}
bool ClockBackendBusy(){
  if(activeClockInvocations)return true;
  if(clockRouter){const auto state=clockRouter->Read();if(state.envelopes||state.submitters||state.callbackContexts||state.rootContexts||state.uncertain||clockRouter->HasCurrentContext())return true;}
  if(clockGate){const auto state=clockGate->Read();if(state.phase||state.foreignJobs||state.childJobs||state.runningJobs)return true;}
  return false;
}

bool ValidSerial(uint64_t serial){return uint32_t(serial>>32)==session&&uint32_t(serial)!=0;}
PoseHistoryRegistry::Token Token(void *owner,uint64_t serial){return {owner,uint32_t(serial)};}
bool Shape(const float *base,uint32_t joints){return base&&joints&&joints<=65536;}
bool WorldBindingShape(const float *base,uint32_t joints){return (!base&&!joints)||Shape(base,joints);}
bool WorldShape(const TFUWorldToken *token){return token&&token->geometry&&token->owner.owner&&
  !token->reserved&&!token->owner.reserved&&token->binding&&WorldBindingShape(reinterpret_cast<const float *>(token->base),token->joints);}
PoseHistoryRegistry::WorldToken WorldToken(const TFUWorldToken &t){
  return {reinterpret_cast<void *>(t.geometry),t.binding,Token(reinterpret_cast<void *>(t.owner.owner),t.owner.serial),reinterpret_cast<const float *>(t.base),t.joints};
}
}
extern "C" uint32_t __stdcall TFUHistoryABI(){return TFU_POSE_HISTORY_ABI;}
extern "C" int __stdcall TFUHistoryInitialize(){
  try {Lock lock;if(closing)return TFU_HISTORY_NOT_INITIALIZED;if(registry)return TFU_HISTORY_OK;if(session==UINT32_MAX||clockEpoch==UINT64_MAX)return TFU_HISTORY_EPOCH_EXHAUSTED;
    auto next=std::make_unique<PoseHistoryRegistry>();auto nextCameras=std::make_unique<tfu::CameraHistoryRegistry>(uint64_t(session)+1);
    auto nextGate=std::make_unique<tfu::ClockDomainGate>(clockEpoch+1);auto nextRouter=std::make_unique<tfu::ClockJobRouter>(*nextGate);
    registry=std::move(next);cameras=std::move(nextCameras);clockGate=std::move(nextGate);clockRouter=std::move(nextRouter);++clockEpoch;++session;return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryShutdown(){
  if(uploadScope||invocationDepth)return TFU_HISTORY_REJECTED;
  try {Lock lock;if(closing||ClockBackendBusy())return TFU_HISTORY_REJECTED;closing=true;
    while(activeInvocations)if(!SleepConditionVariableSRW(&invocationIdle,&moduleMutex,INFINITE,0)){closing=false;return TFU_HISTORY_INTERNAL_ERROR;}
    if(ClockBackendBusy()){closing=false;return TFU_HISTORY_REJECTED;}
    if(clockRouter){clockRouter->Close();if(!clockRouter->CanDetach()){closing=false;return TFU_HISTORY_REJECTED;}}
    clockRouter.reset();clockGate.reset();cameras.reset();registry.reset();closing=false;return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
namespace {
enum class ClockOperation {Submit,Acquire,Invoke,Drain,Release,Close,Reconcile};
struct ClockRequest {
  ClockOperation operation;tfu::ClockDomainGate::Scope scope={};uint32_t timeout=0;
  TFUHistoryClockCallback callback=nullptr;void *context=nullptr;int32_t argument=0;uint32_t label=0;
  uint32_t *result=nullptr;TFUClockScope *output=nullptr;
};
int EnterClockInvocation(tfu::ClockDomainGate **gate,tfu::ClockJobRouter **router,TFUHistoryNativeClockSubmit *submit){
  Lock lock;if(!registry||!clockGate||!clockRouter||closing)return TFU_HISTORY_NOT_INITIALIZED;
  if(activeInvocations==ULONG_MAX||activeClockInvocations==ULONG_MAX)return TFU_HISTORY_INTERNAL_ERROR;
  ++activeInvocations;++activeClockInvocations;++invocationDepth;*gate=clockGate.get();*router=clockRouter.get();*submit=nativeClockSubmit;return TFU_HISTORY_OK;
}
void LeaveClockInvocation(){Lock lock;--activeClockInvocations;--activeInvocations;--invocationDepth;if(!activeInvocations)WakeAllConditionVariable(&invocationIdle);}
int ReconcileClockWork(tfu::ClockDomainGate *gate,tfu::ClockJobRouter *router){
  try {Lock lock;if(activeInvocations!=1||activeClockInvocations!=1||clockGate.get()!=gate||clockRouter.get()!=router)return TFU_HISTORY_REJECTED;
    const auto jobs=router->Read();const auto domain=gate->Read();
    if(jobs.accepting||jobs.envelopes||jobs.submitters||jobs.callbackContexts||jobs.rootContexts||domain.phase||domain.foreignJobs||domain.childJobs||domain.runningJobs)return TFU_HISTORY_REJECTED;
    if(clockEpoch==UINT64_MAX)return TFU_HISTORY_EPOCH_EXHAUSTED;
    auto next=std::make_unique<tfu::ClockJobRouter>(*gate);
    if(!router->ReconcileAfterNativeQuiescence()||!gate->ResetAfterNativeReconcile(clockEpoch+1))return TFU_HISTORY_REJECTED;
    clockRouter=std::move(next);++clockEpoch;return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
int InvokeClockRequest(ClockRequest *request){
  tfu::ClockDomainGate *gate=nullptr;tfu::ClockJobRouter *router=nullptr;TFUHistoryNativeClockSubmit submit=nullptr;
  int status=EnterClockInvocation(&gate,&router,&submit);if(status!=TFU_HISTORY_OK)return status;
  __try {
    bool accepted=false;
    switch(request->operation){
      case ClockOperation::Submit:
        if(!submit){status=TFU_HISTORY_NOT_INITIALIZED;break;}
        accepted=router->Submit(submit,request->callback,request->context,request->argument,request->label,request->result);status=accepted?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;break;
      case ClockOperation::Acquire:{tfu::ClockDomainGate::Scope scope;accepted=gate->Acquire(request->timeout,&scope);if(accepted)*request->output={scope.epoch,scope.id};status=accepted?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;break;}
      case ClockOperation::Invoke:accepted=router->WithScope(request->scope,request->callback,request->context,request->argument);status=accepted?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;break;
      case ClockOperation::Drain:accepted=gate->Drain(request->scope,request->timeout);status=accepted?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;break;
      case ClockOperation::Release:accepted=gate->ReleaseAfterClockRestore(request->scope);status=accepted?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;break;
      case ClockOperation::Close:router->Close();status=TFU_HISTORY_OK;break;
      case ClockOperation::Reconcile:status=ReconcileClockWork(gate,router);break;
    }
  }__finally {LeaveClockInvocation();}
  return status;
}
bool ClockShape(const TFUClockScope *scope){return scope&&scope->epoch&&scope->id;}
}
extern "C" int __stdcall TFUHistorySetClockSubmitFunction(TFUHistoryNativeClockSubmit original){
  if(!original||reinterpret_cast<void *>(original)==reinterpret_cast<void *>(TFUHistorySubmitClockJob))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(nativeClockSubmit&&nativeClockSubmit!=original))return TFU_HISTORY_REJECTED;nativeClockSubmit=original;return TFU_HISTORY_OK;
}
extern "C" int __stdcall TFUHistorySubmitClockJob(TFUHistoryClockCallback callback,void *state,int32_t argument,uint32_t label,uint32_t *result){
  if(!callback||!result)return TFU_HISTORY_BAD_ARGUMENT;
  ClockRequest request={ClockOperation::Submit};request.callback=callback;request.context=state;request.argument=argument;request.label=label;request.result=result;return InvokeClockRequest(&request);
}
extern "C" int __stdcall TFUHistoryAcquireClockScope(uint32_t timeout,TFUClockScope *output){
  if(!output)return TFU_HISTORY_BAD_ARGUMENT;*output={};ClockRequest request={ClockOperation::Acquire};request.timeout=timeout;request.output=output;return InvokeClockRequest(&request);
}
extern "C" int __stdcall TFUHistoryInvokeClockScope(const TFUClockScope *scope,TFUHistoryClockCallback body,void *context,int32_t argument){
  if(!ClockShape(scope)||!body)return TFU_HISTORY_BAD_ARGUMENT;ClockRequest request={ClockOperation::Invoke};request.scope={scope->epoch,scope->id};request.callback=body;request.context=context;request.argument=argument;return InvokeClockRequest(&request);
}
extern "C" int __stdcall TFUHistoryDrainClockScope(const TFUClockScope *scope,uint32_t timeout){
  if(!ClockShape(scope))return TFU_HISTORY_BAD_ARGUMENT;ClockRequest request={ClockOperation::Drain};request.scope={scope->epoch,scope->id};request.timeout=timeout;return InvokeClockRequest(&request);
}
extern "C" int __stdcall TFUHistoryReleaseClockScopeAfterRestore(const TFUClockScope *scope){
  if(!ClockShape(scope))return TFU_HISTORY_BAD_ARGUMENT;ClockRequest request={ClockOperation::Release};request.scope={scope->epoch,scope->id};return InvokeClockRequest(&request);
}
extern "C" int __stdcall TFUHistoryCloseClockJobs(){ClockRequest request={ClockOperation::Close};return InvokeClockRequest(&request);}
extern "C" int __stdcall TFUHistoryReconcileClockBackend(){ClockRequest request={ClockOperation::Reconcile};return InvokeClockRequest(&request);}
extern "C" int __stdcall TFUHistoryClockStatus(TFUClockStatus *output){
  if(!output||output->bytes!=sizeof(*output))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;*output={sizeof(*output)};if(!registry||!clockGate||!clockRouter||closing)return TFU_HISTORY_NOT_INITIALIZED;
  const auto domain=clockGate->Read();const auto jobs=clockRouter->Read();
  *output={sizeof(*output),domain.phase,domain.foreignJobs,domain.childJobs,domain.runningJobs,jobs.envelopes,jobs.submitters,jobs.callbackContexts,jobs.rootContexts,jobs.accepting?1u:0u,jobs.uncertain?1u:0u,domain.failed?1u:0u,domain.quarantined?1u:0u,activeClockInvocations};return TFU_HISTORY_OK;
}
namespace {
bool CameraShape(const TFUCameraToken *token){return token&&token->camera&&!token->reserved&&token->generation&&token->epoch;}
tfu::CameraHistoryRegistry::Token CameraToken(const TFUCameraToken &token){return {token.camera,token.generation,token.epoch};}
int PresentCameraWork(const TFUCameraToken *token,float alpha,const void *observed,void *output,TFUHistoryNativeCameraSetter original){
  try {tfu::CameraHistoryRegistry *cameraRegistry=nullptr;
    {Lock lock;if(!registry||!cameras||closing)return TFU_HISTORY_NOT_INITIALIZED;cameraRegistry=cameras.get();}
    return cameraRegistry->Present(CameraToken(*token),alpha,observed,output,original)?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
}
extern "C" int __stdcall TFUHistoryBeginCamera(void *camera,TFUCameraToken *output){
  if(!camera||!output)return TFU_HISTORY_BAD_ARGUMENT;*output={};
  try {Lock lock;if(!registry||!cameras||closing)return TFU_HISTORY_NOT_INITIALIZED;const auto token=cameras->Begin(reinterpret_cast<uintptr_t>(camera));if(!token.generation)return TFU_HISTORY_EPOCH_EXHAUSTED;*output={uint32_t(token.owner),0,token.generation,token.epoch};return TFU_HISTORY_OK;}catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryRetireCamera(const TFUCameraToken *token){
  if(!CameraShape(token))return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||!cameras||closing)return TFU_HISTORY_NOT_INITIALIZED;return cameras->Retire(CameraToken(*token))?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;}catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryCameraToken(void *camera,TFUCameraToken *output){
  if(!camera||!output)return TFU_HISTORY_BAD_ARGUMENT;*output={};
  try {Lock lock;if(!registry||!cameras||closing)return TFU_HISTORY_NOT_INITIALIZED;
    const auto token=cameras->Current(reinterpret_cast<uintptr_t>(camera));if(!token.generation)return TFU_HISTORY_REJECTED;
    *output={uint32_t(token.owner),0,token.generation,token.epoch};return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistorySetCameraCaptureStep(uint64_t step,uint32_t cut){
  if(cut>1||(!step&&cut))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;cameraCaptureStep=0;cameraCaptureCut=0;
  if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;
  cameraCaptureSession=session;cameraCaptureStep=step;cameraCaptureCut=cut;return TFU_HISTORY_OK;
}
extern "C" int __stdcall TFUHistorySetCameraFunctions(TFUHistoryNativeCameraRoutine create,TFUHistoryNativeCameraRoutine destroy,TFUHistoryNativeCameraRoutine update){
  if(!create||!destroy||!update)return TFU_HISTORY_BAD_ARGUMENT;
  for(auto function:{create,destroy,update})
    if(reinterpret_cast<void *>(function)==reinterpret_cast<void *>(TFUHistoryCameraCreateHook)||reinterpret_cast<void *>(function)==reinterpret_cast<void *>(TFUHistoryCameraDestroyHook)||reinterpret_cast<void *>(function)==reinterpret_cast<void *>(TFUHistoryCameraUpdateHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(nativeCameraCreate&&nativeCameraCreate!=create)||(nativeCameraDestroy&&nativeCameraDestroy!=destroy)||(nativeCameraUpdate&&nativeCameraUpdate!=update))return TFU_HISTORY_REJECTED;
  nativeCameraCreate=create;nativeCameraDestroy=destroy;nativeCameraUpdate=update;return TFU_HISTORY_OK;
}
namespace {
TFUHistoryNativeCameraRoutine CameraRoutine(uint32_t mode){Lock lock;return mode==0?nativeCameraCreate:mode==1?nativeCameraDestroy:nativeCameraUpdate;}
void ForgetNativeCamera(void *context){
  try {Lock lock;if(cameras){auto token=cameras->Current(reinterpret_cast<uintptr_t>(context));if(token.generation)cameras->Retire(token);}}
  catch(...){/* Do not alter native destruction. */}
}
void RegisterNativeCamera(void *context){
  TFUCameraToken ignored={};TFUHistoryBeginCamera(context,&ignored);
}
bool SnapshotNativeCamera(uint32_t owner,void *state){
  if(!owner||owner>UINTPTR_MAX-0x4B70)return false;
  // Read caller-owned native storage before taking either registry lock. A
  // failed read must not unwind past locks compiled with ordinary C++ EH.
  __try {std::memcpy(state,reinterpret_cast<const void *>(owner+0x4950),0x220);return true;}
  __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void CaptureNativeCamera(const TFUCameraToken *token,uint64_t step,uint32_t cut){
  TFUCameraToken current={};
  if(TFUHistoryCameraToken(reinterpret_cast<void *>(token->camera),&current)!=TFU_HISTORY_OK||current.generation!=token->generation||current.epoch!=token->epoch)return;
  __declspec(align(16)) unsigned char state[0x220];
  if(SnapshotNativeCamera(token->camera,state))TFUHistoryCaptureCamera(token,step,state,cut);
}
void DescribeCameraUpdate(void *context,TFUCameraToken *output,uint64_t *step,uint32_t *cut){
  Lock lock;if(!cameras||closing||cameraCaptureSession!=session||!cameraCaptureStep)return;
  const auto token=cameras->Current(reinterpret_cast<uintptr_t>(context));if(!token.generation)return;
  *output={uint32_t(token.owner),0,token.generation,token.epoch};*step=cameraCaptureStep;*cut=cameraCaptureCut;
}
uint32_t __cdecl ObserveCameraCreate(void *context){
  auto original=CameraRoutine(0);if(!original)return 0;bool available=false;
  if(!EnterInvocation(&available))return original(context);
  uint32_t result=0;
  __try {ForgetNativeCamera(context);result=original(context);if(available&&result==reinterpret_cast<uint32_t>(context))RegisterNativeCamera(context);}
  __finally {LeaveInvocation();}return result;
}
uint32_t __cdecl ObserveCameraDestroy(void *context){
  auto original=CameraRoutine(1);if(!original)return 0;bool available=false;
  if(!EnterInvocation(&available))return original(context);
  uint32_t result=0;
  __try {ForgetNativeCamera(context);result=original(context);}
  __finally {LeaveInvocation();}return result;
}
uint32_t __cdecl ObserveCameraUpdate(void *context){
  auto original=CameraRoutine(2);if(!original)return 0;bool available=false;
  if(!EnterInvocation(&available))return original(context);
  TFUCameraToken token={};uint64_t step=0;uint32_t cut=0;
  if(available)DescribeCameraUpdate(context,&token,&step,&cut);
  const ULONG previousDepth=cameraUpdateDepth++;uint32_t result=0;
  __try {result=original(context);if(!previousDepth&&step)CaptureNativeCamera(&token,step,cut);}
  __finally {--cameraUpdateDepth;LeaveInvocation();}return result;
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryCameraCreateHook(){__asm {push ecx
  call ObserveCameraCreate
  add esp,4
  ret}}
extern "C" __declspec(naked) void __cdecl TFUHistoryCameraDestroyHook(){__asm {push ecx
  call ObserveCameraDestroy
  add esp,4
  ret}}
extern "C" __declspec(naked) void __cdecl TFUHistoryCameraUpdateHook(){__asm {push ecx
  call ObserveCameraUpdate
  add esp,4
  ret}}
extern "C" int __stdcall TFUHistoryCaptureCamera(const TFUCameraToken *token,uint64_t step,const void *state,uint32_t cut){
  if(!CameraShape(token)||!step||!state||cut>1)return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||!cameras||closing)return TFU_HISTORY_NOT_INITIALIZED;return cameras->Capture(CameraToken(*token),step,state,cut!=0)?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;}catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryPresentCamera(const TFUCameraToken *token,float alpha,const void *observed,void *output,TFUHistoryNativeCameraSetter original){
  if(!CameraShape(token)||!observed||!output||!original||!std::isfinite(alpha)||alpha<0||alpha>1)return TFU_HISTORY_BAD_ARGUMENT;
  bool available=false;if(!EnterInvocation(&available))return TFU_HISTORY_INTERNAL_ERROR;int status=TFU_HISTORY_NOT_INITIALIZED;
  __try {if(available)status=PresentCameraWork(token,alpha,observed,output,original);}
  __finally {LeaveInvocation();}
  return status;
}
extern "C" int __stdcall TFUHistorySetCameraSetter(TFUHistoryNativeCameraSetter original){
  if(!original||reinterpret_cast<void *>(original)==reinterpret_cast<void *>(TFUHistoryCameraCopyHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(nativeCameraSetter&&nativeCameraSetter!=original))return TFU_HISTORY_REJECTED;
  nativeCameraSetter=original;return TFU_HISTORY_OK;
}
namespace {
void CameraCopyWork(const void *source,void *view){
  try {
    tfu::CameraHistoryRegistry *cameraRegistry=nullptr;tfu::CameraHistoryRegistry::Token token;
    TFUHistoryNativeCameraSetter setter=nullptr;float alpha=0;
    {Lock lock;
      if(!registry||!cameras||closing||!renderEnabled||renderSession!=session||!nativeCameraSetter)return;
      const uintptr_t state=reinterpret_cast<uintptr_t>(source);
      if(state<0x4950||!view||reinterpret_cast<uintptr_t>(view)>UINTPTR_MAX-0x240)return;
      cameraRegistry=cameras.get();token=cameraRegistry->Current(state-0x4950);
      if(!token.generation)return;
      setter=nativeCameraSetter;alpha=renderAlpha;
    }
    cameraRegistry->Present(token,alpha,source,static_cast<unsigned char *>(view)+0x20,setter);
  }catch(...){/* Keep the native copy on allocation or math failure. */}
}
uintptr_t __stdcall ObserveCameraCopy(const void *source,void *view){
  if(cameraCopyActive)return reinterpret_cast<uintptr_t>(source);
  bool available=false;if(!EnterInvocation(&available))return reinterpret_cast<uintptr_t>(source);
  cameraCopyActive=true;
  __try {
    __try {if(available)CameraCopyWork(source,view);}
    __except(EXCEPTION_EXECUTE_HANDLER){/* Private rebuild failed; retain native view. */}
  }__finally {cameraCopyActive=false;LeaveInvocation();}
  return reinterpret_cast<uintptr_t>(source);
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryCameraCopyHook(){
  __asm {
    pushfd
    pushad
    mov ebp,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    fninit
    fldcw word ptr [esp]
    push dword ptr [ebp+16]
    push dword ptr [ebp+24]
    call ObserveCameraCopy
    mov dword ptr [ebp+28],eax
    fxrstor [esp]
    mov esp,ebp
    popad
    popfd
    ret
  }
}
extern "C" int __stdcall TFUHistoryBegin(void *owner,TFUHistoryToken *output){
  if(!owner||!output)return TFU_HISTORY_BAD_ARGUMENT;*output={};
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;auto token=registry->Begin(owner);
    if(!token.serial||token.serial>UINT32_MAX){registry->Retire(token);return TFU_HISTORY_EPOCH_EXHAUSTED;}
    output->owner=reinterpret_cast<uint32_t>(owner);output->serial=(uint64_t(session)<<32)|token.serial;return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryActivate(void *owner,uint64_t serial,const float *base,uint32_t joints){
  if(!owner||!Shape(base,joints))return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;if(!ValidSerial(serial))return TFU_HISTORY_REJECTED;
    return registry->Activate(Token(owner,serial),base,joints)?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryCapture(void *owner,uint64_t serial,const float *base,uint32_t joints){
  if(!owner||!Shape(base,joints))return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;if(!ValidSerial(serial))return TFU_HISTORY_REJECTED;
    return registry->Capture(Token(owner,serial),base,joints)?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryActivateWorld(void *owner,uint64_t serial){
  if(!owner)return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;if(!ValidSerial(serial))return TFU_HISTORY_REJECTED;
    return registry->ActivateWorld(Token(owner,serial))?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryBindWorld(void *geometry,void *owner,uint64_t serial){
  if(!geometry||!owner)return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;if(!ValidSerial(serial))return TFU_HISTORY_REJECTED;
    return registry->BindWorld(geometry,Token(owner,serial))?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryBind(void *geometry,void *owner,uint64_t serial,const float *base,uint32_t joints){
  if(!geometry||!owner||!Shape(base,joints))return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;if(!ValidSerial(serial))return TFU_HISTORY_REJECTED;
    return registry->Bind(geometry,Token(owner,serial),base,joints)?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistorySample(void *geometry,const float *base,uint32_t joints,float alpha,float *output){
  if(!geometry||!Shape(base,joints)||!output||!std::isfinite(alpha)||alpha<0||alpha>1)return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;
    return registry->Acquire(geometry,base,joints).Sample(alpha,output)?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryForgetGeometry(void *geometry){
  if(!geometry)return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;registry->ForgetGeometry(geometry);return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryWorldBinding(void *geometry,const float *base,uint32_t joints,TFUWorldToken *output){
  if(!geometry||!WorldBindingShape(base,joints)||!output)return TFU_HISTORY_BAD_ARGUMENT;*output={};
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;
    const auto binding=registry->WorldBinding(geometry,base,joints);if(!binding.binding)return TFU_HISTORY_REJECTED;
    output->geometry=reinterpret_cast<uint32_t>(geometry);output->base=reinterpret_cast<uint32_t>(base);output->joints=joints;
    output->owner={reinterpret_cast<uint32_t>(binding.owner.owner),0,(uint64_t(session)<<32)|binding.owner.serial};output->binding=binding.binding;
    return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryCaptureWorld(const TFUWorldToken *token,uint64_t step,const float *matrix,uint32_t discontinuity){
  if(!WorldShape(token)||!step||!matrix||discontinuity>1)return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;if(!ValidSerial(token->owner.serial))return TFU_HISTORY_REJECTED;
    return registry->CaptureWorld(WorldToken(*token),step,matrix,discontinuity!=0)?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistorySampleWorld(const TFUWorldToken *token,float alpha,float *output){
  if(!WorldShape(token)||!output||!std::isfinite(alpha)||alpha<0||alpha>1)return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;if(!ValidSerial(token->owner.serial))return TFU_HISTORY_REJECTED;
    return registry->SampleWorld(WorldToken(*token),alpha,output)?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryRetire(void *owner,uint64_t serial){
  if(!owner)return TFU_HISTORY_BAD_ARGUMENT;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;if(!ValidSerial(serial))return TFU_HISTORY_REJECTED;
    return registry->Retire(Token(owner,serial))?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistoryPalette(void *geometry,const float *base,uint32_t joints,float alpha,
    const uint8_t *remap,uint32_t drawJoints,TFUHistoryBasisBuilder basisBuilder,float *output,uint32_t capacityFloats){
  if(!geometry||!Shape(base,joints)||!remap||!drawJoints||drawJoints>256||!basisBuilder||!output||
      capacityFloats<drawJoints*12||!std::isfinite(alpha)||alpha<0||alpha>1)return TFU_HISTORY_BAD_ARGUMENT;
  try {
    Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;
    auto lease=registry->Acquire(geometry,base,joints);if(!lease.HasRecord())return TFU_HISTORY_REJECTED;
    for(uint32_t i=0;i<drawJoints;++i)if(remap[i]>=joints)return TFU_HISTORY_REJECTED;
    std::vector<unsigned char> storage(joints*32+15);
    float *poses=reinterpret_cast<float *>((reinterpret_cast<uintptr_t>(storage.data())+15)&~uintptr_t(15));
    if(!lease.Sample(alpha,poses))return TFU_HISTORY_REJECTED;
    std::vector<float> palette(drawJoints*12);
    for(uint32_t i=0;i<drawJoints;++i){const float *pose=poses+remap[i]*8;__declspec(align(16)) float x[4]={},y[4]={},z[4]={};
      basisBuilder(x,y,z,pose+4);
      for(unsigned row=0;row<3;++row){float *p=palette.data()+i*12+row*4;p[0]=x[row];p[1]=y[row];p[2]=z[row];p[3]=pose[row];}
    }
    memcpy(output,palette.data(),palette.size()*sizeof(float));return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}

extern "C" int32_t __stdcall TFUHistoryUpload(void *device,uint32_t first,const float *data,uint32_t count){
  UploadScope *scope=uploadScope;
  if(scope&&scope->data&&!scope->consumed&&scope->first==first&&scope->count==count){scope->consumed=true;data=scope->data;}
  auto table=*static_cast<uintptr_t **>(device);
  auto original=reinterpret_cast<int32_t (__stdcall *)(void *,uint32_t,const float *,uint32_t)>(table[94]);
  return original(device,first,data,count);
}
extern "C" int __stdcall TFUHistoryInvokePalette(const TFUPaletteCall *call){
  if(!call||call->bytes!=sizeof(TFUPaletteCall)||!call->nativeDraw||!call->context||!call->parameters)return TFU_HISTORY_BAD_ARGUMENT;
  bool available=false;if(!EnterInvocation(&available))return TFU_HISTORY_INTERNAL_ERROR;
  UploadScope scope={nullptr,call->firstRegister,0,false};UploadScope *previous=uploadScope;
  HANDLE heap=GetProcessHeap();float *palette=nullptr;uint32_t preparation=TFU_HISTORY_NOT_INITIALIZED;
  __try {
    if(available){
      preparation=TFU_HISTORY_BAD_ARGUMENT;
      if(call->drawJoints&&call->drawJoints<=256){
        palette=static_cast<float *>(HeapAlloc(heap,0,call->drawJoints*48));
        preparation=palette?TFUHistoryPalette(call->geometry,call->base,call->joints,call->alpha,call->remap,call->drawJoints,call->basisBuilder,palette,call->drawJoints*12):TFU_HISTORY_INTERNAL_ERROR;
        if(preparation==TFU_HISTORY_OK){scope.data=palette;scope.count=call->drawJoints*3;}
      }
    }
    uploadScope=&scope;
    call->nativeDraw(call->context,call->parameters,call->argument);
  } __finally {
    uploadScope=previous;
    if(call->outcome){call->outcome->preparationStatus=preparation;call->outcome->overrideUsed=scope.consumed?1u:0u;}
    if(palette)HeapFree(heap,0,palette);
    LeaveInvocation();
  }
  return TFU_HISTORY_OK;
}

extern "C" int __stdcall TFUHistoryOwnerToken(void *owner,TFUHistoryToken *output){
  if(!owner||!output)return TFU_HISTORY_BAD_ARGUMENT;*output={};
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;auto token=registry->CurrentToken(owner,true);
    if(!token.serial||token.serial>UINT32_MAX)return TFU_HISTORY_REJECTED;
    output->owner=reinterpret_cast<uint32_t>(owner);output->serial=(uint64_t(session)<<32)|token.serial;return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistorySetOwnerFunctions(TFUHistoryNativeInitialize initialize,TFUHistoryNativeRelease release){
  if(!initialize||!release||reinterpret_cast<void *>(initialize)==reinterpret_cast<void *>(TFUHistoryOwnerInitHook)||reinterpret_cast<void *>(release)==reinterpret_cast<void *>(TFUHistoryOwnerReleaseHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations)return TFU_HISTORY_REJECTED;
  if((ownerInitialize&&ownerInitialize!=initialize)||(ownerRelease&&ownerRelease!=release))return TFU_HISTORY_REJECTED;
  ownerInitialize=initialize;ownerRelease=release;return TFU_HISTORY_OK;
}
namespace {
void RetireNativeOwner(void *owner){try {Lock lock;if(registry){auto token=registry->CurrentToken(owner);if(token.serial)registry->Retire(token);}}catch(...){}}
TFUHistoryNativeInitialize NativeInitializeFunction(){Lock lock;return ownerInitialize;}
TFUHistoryNativeRelease NativeReleaseFunction(){Lock lock;return ownerRelease;}
void __cdecl ObserveNativeOwnerInitialize(void *owner,void *metadata,void *external){
  TFUHistoryNativeInitialize original=NativeInitializeFunction();if(!original)return;
  bool available=false;if(!EnterInvocation(&available)){original(owner,metadata,external);return;}
  TFUHistoryToken token={};bool completed=false;
  __try {
    if(available)TFUHistoryBegin(owner,&token);else RetireNativeOwner(owner);
    original(owner,metadata,external);completed=true;
    if(token.serial){auto fields=static_cast<uint32_t *>(owner);int status=TFU_HISTORY_REJECTED;
      if(fields[0]&&fields[1]==reinterpret_cast<uint32_t>(metadata)&&fields[4])status=TFUHistoryActivate(owner,token.serial,reinterpret_cast<const float *>(fields[4]),static_cast<uint32_t *>(metadata)[57]);
      if(status!=TFU_HISTORY_OK)TFUHistoryRetire(owner,token.serial);
    }
  } __finally {if(!completed&&token.serial)TFUHistoryRetire(owner,token.serial);LeaveInvocation();}
}
void __cdecl ObserveNativeOwnerRelease(void *owner){
  TFUHistoryNativeRelease original=NativeReleaseFunction();if(!original)return;
  bool available=false;if(!EnterInvocation(&available)){original(owner);return;}
  __try {RetireNativeOwner(owner);original(owner);}__finally {LeaveInvocation();}
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryOwnerInitHook(){
  __asm {push dword ptr [esp+8]
         push dword ptr [esp+8]
         push ecx
         call ObserveNativeOwnerInitialize
         add esp,12
         ret 8}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryOwnerReleaseHook(){
  __asm {push ecx
         call ObserveNativeOwnerRelease
         add esp,4
         ret}
}

extern "C" int __stdcall TFUHistorySetBindingFunction(TFUHistoryNativeBind bind){
  if(!bind||reinterpret_cast<void *>(bind)==reinterpret_cast<void *>(TFUHistoryGeometryBindHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(geometryBind&&geometryBind!=bind))return TFU_HISTORY_REJECTED;
  geometryBind=bind;return TFU_HISTORY_OK;
}
namespace {
TFUHistoryNativeBind NativeBindingFunction(){Lock lock;return geometryBind;}
void ForgetNativeGeometry(void *geometry){try {Lock lock;if(registry)registry->ForgetGeometry(geometry);}catch(...){}}
void RegisterNativeGeometry(void *geometry,const float *base){
  try {Lock lock;if(!registry||closing||!base)return;
    auto fields=static_cast<uint32_t *>(geometry);auto root=reinterpret_cast<const uint32_t *>(fields[25]);if(!root)return;
    auto skeleton=reinterpret_cast<const uint32_t *>(root[9]);if(!skeleton||!skeleton[4]||skeleton[4]>65536)return;
    auto skin=reinterpret_cast<const uint32_t *>(fields[16]);if(!skin||fields[37]!=reinterpret_cast<uint32_t>(base)||skin[38]!=fields[37])return;
    auto token=registry->UniqueTokenForBase(base,skeleton[4]);if(token.serial)registry->Bind(geometry,token,base,skeleton[4]);
  }catch(...){/* Observation failure preserves the already completed native binding. */}
}
void __cdecl ObserveNativeGeometryBinding(void *geometry,const float *base){
  auto original=NativeBindingFunction();if(!original)return;
  bool available=false;if(!EnterInvocation(&available)){original(geometry,base);return;}
  __try {ForgetNativeGeometry(geometry);original(geometry,base);if(available)RegisterNativeGeometry(geometry,base);}
  __finally {LeaveInvocation();}
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryGeometryBindHook(){
  __asm {push dword ptr [esp+4]
         push ecx
         call ObserveNativeGeometryBinding
         add esp,8
         ret 4}
}

extern "C" int __stdcall TFUHistorySetBatchFunction(TFUHistoryNativeBatch batch){
  if(!batch||reinterpret_cast<void *>(batch)==reinterpret_cast<void *>(TFUHistoryAnimationBatchHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(animationBatch&&animationBatch!=batch))return TFU_HISTORY_REJECTED;
  animationBatch=batch;return TFU_HISTORY_OK;
}
namespace {
TFUHistoryNativeBatch NativeBatchFunction(){Lock lock;return animationBatch;}
void CaptureNativeBatch(void *entries,int32_t count,int32_t phase){
  if(phase!=2||!entries||count<=0||count>65536)return;
  try {Lock lock;if(!registry||closing)return;if(++capturePass==0)++capturePass;
    auto bytes=static_cast<const unsigned char *>(entries);
    for(int32_t i=0;i<count;++i){auto entry=bytes+i*16;if(!entry[4]||*reinterpret_cast<const LONG *>(entry+8)!=0)continue;
      auto actor=*reinterpret_cast<const unsigned char *const *>(entry);if(!actor||*reinterpret_cast<const uint32_t *>(actor+0x104)!=7)continue;
      auto rig=*reinterpret_cast<unsigned char *const *>(actor+0xF8);if(!rig)continue;
      void *owner=rig+0x28;auto token=registry->CurrentToken(owner,true);if(!token.serial)continue;
      try {auto fields=static_cast<const uint32_t *>(owner);auto metadata=reinterpret_cast<const uint32_t *>(fields[1]);
        if(!fields[0]||!metadata||!fields[4]||!registry->CaptureOnce(token,reinterpret_cast<const float *>(fields[4]),metadata[57],capturePass))registry->Retire(token);
      }catch(...){registry->Retire(token);}
    }
  }catch(...){/* Completed native work is never undone by observation failure. */}
}
void __cdecl ObserveNativeAnimationBatch(void *manager,void *entries,int32_t count,int32_t phase){
  auto original=NativeBatchFunction();if(!original)return;
  bool available=false;if(!EnterInvocation(&available)){original(manager,entries,count,phase);return;}
  __try {original(manager,entries,count,phase);if(available)CaptureNativeBatch(entries,count,phase);}
  __finally {LeaveInvocation();}
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryAnimationBatchHook(){
  __asm {push dword ptr [esp+12]
         push dword ptr [esp+12]
         push dword ptr [esp+12]
         push ecx
         call ObserveNativeAnimationBatch
         add esp,16
         ret 12}
}

extern "C" int __stdcall TFUHistorySetGeometryCleanupFunction(TFUHistoryNativeRelease cleanup){
  if(!cleanup||reinterpret_cast<void *>(cleanup)==reinterpret_cast<void *>(TFUHistoryGeometryCleanupHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(geometryCleanup&&geometryCleanup!=cleanup))return TFU_HISTORY_REJECTED;
  geometryCleanup=cleanup;return TFU_HISTORY_OK;
}
namespace {
TFUHistoryNativeRelease NativeGeometryCleanupFunction(){Lock lock;return geometryCleanup;}
void __cdecl ObserveNativeGeometryCleanup(void *geometry){
  auto original=NativeGeometryCleanupFunction();if(!original)return;
  bool available=false;if(!EnterInvocation(&available)){original(geometry);return;}
  __try {ForgetNativeGeometry(geometry);original(geometry);}
  __finally {LeaveInvocation();}
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryGeometryCleanupHook(){
  __asm {push ecx
         call ObserveNativeGeometryCleanup
         add esp,4
         ret}
}

extern "C" int __stdcall TFUHistorySetDrawFunctions(TFUHistoryNativeDraw draw,TFUHistoryBasisBuilder basisBuilder){
  if(!draw||!basisBuilder||reinterpret_cast<void *>(draw)==reinterpret_cast<void *>(TFUHistoryDrawHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(nativeDraw&&nativeDraw!=draw)||(nativeBasis&&nativeBasis!=basisBuilder))return TFU_HISTORY_REJECTED;
  nativeDraw=draw;nativeBasis=basisBuilder;return TFU_HISTORY_OK;
}
extern "C" int __stdcall TFUHistorySetRenderPhase(uint32_t enabled,float alpha){
  if(enabled>1||(enabled&&(!std::isfinite(alpha)||alpha<0||alpha>1)))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;renderEnabled=false;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;
  renderSession=session;renderAlpha=enabled?alpha:0;renderEnabled=enabled!=0;return TFU_HISTORY_OK;
}
namespace {
void DescribeNativeDraw(TFUPaletteCall *call){
  Lock lock;call->nativeDraw=nativeDraw;call->basisBuilder=nativeBasis;
  if(!registry||closing||!renderEnabled||renderSession!=session||uploadScope)return;
  auto context=static_cast<const uint32_t *>(call->context);
  auto drawable=reinterpret_cast<const uint32_t *>(context[0]);
  auto metadata=reinterpret_cast<const uint32_t *>(drawable[78]);if(!metadata)return;
  auto geometry=reinterpret_cast<const uint32_t *>(context[3]);
  auto skin=reinterpret_cast<const uint32_t *>(geometry[16]);
  auto base=reinterpret_cast<const float *>(skin[38]);if(!base)return;
  size_t count=registry->BoundJointCount(const_cast<uint32_t *>(geometry),base);if(!count)return;
  call->geometry=const_cast<uint32_t *>(geometry);call->base=base;call->joints=static_cast<uint32_t>(count);
  call->alpha=renderAlpha;call->remap=reinterpret_cast<const uint8_t *>(metadata[12]);
  call->drawJoints=metadata[11];call->firstRegister=static_cast<const uint32_t *>(call->parameters)[4];
}
void __cdecl ObserveNativeDraw(void *context,void *parameters,uint32_t argument){
  TFUPaletteCall call={};call.bytes=sizeof(call);call.context=context;call.parameters=parameters;call.argument=argument;
  DescribeNativeDraw(&call);if(call.nativeDraw)TFUHistoryInvokePalette(&call);
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryDrawHook(){
  __asm {push dword ptr [esp+8]
         push dword ptr [esp+8]
         push ecx
         call ObserveNativeDraw
         add esp,12
         ret 8}
}
extern "C" int __stdcall TFUHistorySetWorldDrawFunction(TFUHistoryNativeWorldDraw draw){
  if(!draw||reinterpret_cast<void *>(draw)==reinterpret_cast<void *>(TFUHistoryWorldDrawHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(nativeWorldDraw&&nativeWorldDraw!=draw))return TFU_HISTORY_REJECTED;
  nativeWorldDraw=draw;return TFU_HISTORY_OK;
}
namespace {
TFUHistoryNativeWorldDraw WorldDrawFunction(){Lock lock;return nativeWorldDraw;}
bool PrepareNativeWorld(void *raw,float *output){
  Lock lock;if(!registry||closing||!renderEnabled||renderSession!=session)return false;
  auto command=static_cast<const uint32_t *>(raw);if(!command||!command[0]||!command[3])return false;
  // Explicit non-skeletal registration needs no skin or pose allocation.
  // Binding lifetime and the observed current-instance match remain mandatory.
  auto worldOnly=registry->WorldBinding(reinterpret_cast<void *>(command[3]),nullptr,0);
  if(worldOnly.binding)return registry->SampleWorldMatched(worldOnly,
    reinterpret_cast<const float *>(command[0]+0x60),renderAlpha,output);
  auto geometry=reinterpret_cast<const uint32_t *>(command[3]);if(!geometry[16])return false;
  auto skin=reinterpret_cast<const uint32_t *>(geometry[16]);auto base=reinterpret_cast<const float *>(skin[38]);if(!base)return false;
  const size_t joints=registry->BoundJointCount(const_cast<uint32_t *>(geometry),base);if(!joints)return false;
  auto token=registry->WorldBinding(const_cast<uint32_t *>(geometry),base,joints);
  return registry->SampleWorldMatched(token,reinterpret_cast<const float *>(command[0]+0x60),renderAlpha,output);
}
void __cdecl ObserveNativeWorldDraw(void *command,void *device,void *descriptor,void *parameters,void *extras){
  auto original=WorldDrawFunction();if(!original)return;
  bool available=false;if(!EnterInvocation(&available)){original(command,device,descriptor,parameters,extras);return;}
  const bool previous=worldDrawActive;worldDrawActive=true;
  bool nested=false;bool *previousFlag=worldNestedFlag;if(previousFlag)*previousFlag=true;worldNestedFlag=&nested;
  __try {
    __declspec(align(16)) float presented[16];
    const bool ready=available&&!previous&&PrepareNativeWorld(command,presented);
    const bool overridden=ready&&tfu::InvokePrivateWorld(command,presented,device,descriptor,parameters,extras,original);
    // This validated routine uploads parameters, not geometry. Reentrant native
    // draws may overwrite an earlier constant: restore the full native set
    // instead of leaving a mix of native and interpolated matrices behind.
    if(!overridden||nested)original(command,device,descriptor,parameters,extras);
  }__finally {worldNestedFlag=previousFlag;worldDrawActive=previous;LeaveInvocation();}
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryWorldDrawHook(){
  __asm {push dword ptr [esp+16]
         push dword ptr [esp+16]
         push dword ptr [esp+16]
         push dword ptr [esp+16]
         push ecx
         call ObserveNativeWorldDraw
         add esp,20
         ret 16}
}
extern "C" int __stdcall TFUHistorySetWorldPublishFunction(TFUHistoryNativeWorldPublish publish){
  if(!publish||reinterpret_cast<void *>(publish)==reinterpret_cast<void *>(TFUHistoryWorldPublishHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(nativeWorldPublish&&nativeWorldPublish!=publish))return TFU_HISTORY_REJECTED;
  nativeWorldPublish=publish;return TFU_HISTORY_OK;
}
extern "C" int __stdcall TFUHistorySetWorldCaptureStep(uint64_t step){
  Lock lock;worldCaptureStep=0;worldCaptureSession=0;
  if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;
  worldCaptureSession=session;worldCaptureStep=step;return TFU_HISTORY_OK;
}
namespace {
TFUHistoryNativeWorldPublish WorldPublishFunction(){Lock lock;return nativeWorldPublish;}
// moduleMutex is held and registry availability has been checked by caller.
bool CaptureControllerWorldLocked(void *raw,uint64_t step,bool capture,bool controllerOnly=false){
  auto controller=static_cast<const uint32_t *>(raw);if(!controller)return false;
  auto geometry=reinterpret_cast<const uint32_t *>(controller[4]);
  auto skin=geometry&&geometry[16]?reinterpret_cast<const uint32_t *>(geometry[16]):nullptr;
  auto owner=registry->CurrentToken(raw,true);
  if(owner.serial)registry->ReconcileControllerWorld(skin&&!skin[38]?const_cast<uint32_t *>(geometry):nullptr,owner);
  if(!capture||!step||!skin)return false;
  auto worldOnly=registry->WorldBinding(const_cast<uint32_t *>(geometry),nullptr,0);
  if(worldOnly.binding){
    if(controllerOnly&&(worldOnly.owner.owner!=owner.owner||worldOnly.owner.serial!=owner.serial))return false;
    return registry->CaptureWorld(worldOnly,step,reinterpret_cast<const float *>(skin));
  }
  if(controllerOnly)return false;
  auto base=reinterpret_cast<const float *>(skin[38]);if(!base)return false;
  const size_t joints=registry->BoundJointCount(const_cast<uint32_t *>(geometry),base);if(!joints)return false;
  auto token=registry->WorldBinding(const_cast<uint32_t *>(geometry),base,joints);
  return registry->CaptureWorld(token,step,reinterpret_cast<const float *>(skin));
}
void CapturePublishedWorld(void *raw,uint64_t step,uint32_t captureSession){
  try {Lock lock;if(!registry||closing)return;CaptureControllerWorldLocked(raw,step,captureSession==session&&!activePreparations);}
  catch(...){/* Cache failure must not change completed native publication. */}
}
void __cdecl ObserveNativeWorldPublish(void *controller,void *owner){
  auto original=WorldPublishFunction();if(!original)return;
  bool available=false;if(!EnterInvocation(&available)){original(controller,owner);return;}
  const uint64_t step=worldCaptureStep;const uint32_t captureSession=worldCaptureSession;
  __try {original(controller,owner);if(available)CapturePublishedWorld(controller,step,captureSession);}
  __finally {LeaveInvocation();}
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryWorldPublishHook(){
  __asm {push dword ptr [esp+4]
         push ecx
         call ObserveNativeWorldPublish
         add esp,8
         ret 4}
}
extern "C" int __stdcall TFUHistorySetWorldGeometryFunctions(TFUHistoryNativeWorldAssign assign,TFUHistoryNativeWorldClear clear){
  if(!assign||!clear||reinterpret_cast<void *>(assign)==reinterpret_cast<void *>(TFUHistoryWorldGeometryAssignHook)||reinterpret_cast<void *>(clear)==reinterpret_cast<void *>(TFUHistoryWorldGeometryClearHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(nativeWorldAssign&&nativeWorldAssign!=assign)||(nativeWorldClear&&nativeWorldClear!=clear))return TFU_HISTORY_REJECTED;
  nativeWorldAssign=assign;nativeWorldClear=clear;return TFU_HISTORY_OK;
}
namespace {
TFUHistoryNativeWorldAssign ControllerAssign(){Lock lock;return nativeWorldAssign;}
TFUHistoryNativeWorldClear ControllerClear(){Lock lock;return nativeWorldClear;}
void ControllerWorldBinding(void *raw,void *geometry,bool bind){
  try {Lock lock;if(!registry||closing||!raw||!geometry)return;
    auto owner=registry->CurrentToken(raw,true);if(!owner.serial)return;
    auto current=registry->WorldBinding(geometry,nullptr,0);
    if(current.binding){
      if(current.owner.owner!=owner.owner||current.owner.serial!=owner.serial)return;
      if(!bind)registry->ForgetGeometry(geometry);return;
    }
    if(bind)registry->BindWorld(geometry,owner);
  }catch(...){/* Cache registration must not change native lifetime calls. */}
}
uint32_t __cdecl ObserveControllerAssign(void *raw,void *geometry){
  auto original=ControllerAssign();if(!original)return 0;bool available=false;
  if(!EnterInvocation(&available))return original(raw,geometry);uint32_t result=0;
  __try {
    if(available&&raw){auto previous=reinterpret_cast<void *>(static_cast<uint32_t *>(raw)[4]);if(previous!=geometry)ControllerWorldBinding(raw,previous,false);}
    result=original(raw,geometry);
    if(available&&raw)ControllerWorldBinding(raw,reinterpret_cast<void *>(static_cast<uint32_t *>(raw)[4]),true);
  }__finally {LeaveInvocation();}return result;
}
uint32_t __cdecl ObserveControllerClear(void *raw){
  auto original=ControllerClear();if(!original)return 0;bool available=false;
  if(!EnterInvocation(&available))return original(raw);uint32_t result=0;
  __try {if(available&&raw)ControllerWorldBinding(raw,reinterpret_cast<void *>(static_cast<uint32_t *>(raw)[4]),false);result=original(raw);}
  __finally {LeaveInvocation();}return result;
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryWorldGeometryAssignHook(){
  __asm {
    push dword ptr [esp+4]
    push ecx
    call ObserveControllerAssign
    add esp,8
    ret 4
  }
}
extern "C" __declspec(naked) void __cdecl TFUHistoryWorldGeometryClearHook(){
  __asm {
    push ecx
    call ObserveControllerClear
    add esp,4
    ret
  }
}
extern "C" int __stdcall TFUHistorySetWorldControllerFunctions(TFUHistoryNativeWorldCreate create,TFUHistoryNativeWorldClear destroy){
  if(!create||!destroy||reinterpret_cast<void *>(create)==reinterpret_cast<void *>(TFUHistoryWorldControllerCreateHook)||reinterpret_cast<void *>(destroy)==reinterpret_cast<void *>(TFUHistoryWorldControllerDestroyHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(nativeWorldCreate&&nativeWorldCreate!=create)||(nativeWorldDestroy&&nativeWorldDestroy!=destroy))return TFU_HISTORY_REJECTED;
  nativeWorldCreate=create;nativeWorldDestroy=destroy;return TFU_HISTORY_OK;
}
namespace {
TFUHistoryNativeWorldCreate ControllerCreate(){Lock lock;return nativeWorldCreate;}
TFUHistoryNativeWorldClear ControllerDestroy(){Lock lock;return nativeWorldDestroy;}
PoseHistoryRegistry::Token BeginControllerWorld(void *raw){
  try {Lock lock;if(!registry||closing||!raw)return {};auto token=registry->Begin(raw);
    if(token.serial>UINT32_MAX){registry->Retire(token);return {};}return token;
  }catch(...){return {};}
}
bool ActivateControllerWorld(PoseHistoryRegistry::Token token){
  try {Lock lock;return registry&&!closing&&token.serial&&registry->ActivateWorld(token,true);}catch(...){return false;}
}
void RetireControllerWorld(PoseHistoryRegistry::Token token){
  try {Lock lock;if(registry&&token.serial)registry->Retire(token);}catch(...){}
}
void RetireCurrentControllerWorld(void *raw){
  try {Lock lock;if(registry&&raw)registry->Retire(registry->CurrentToken(raw));}catch(...){}
}
uint32_t __cdecl ObserveControllerCreate(void *raw,uint32_t handle,const char *name){
  auto original=ControllerCreate();if(!original)return 0;bool available=false;
  if(!EnterInvocation(&available))return original(raw,handle,name);
  auto token=available?BeginControllerWorld(raw):PoseHistoryRegistry::Token{};uint32_t result=0;bool activated=false;
  __try {result=original(raw,handle,name);if(result==reinterpret_cast<uint32_t>(raw))activated=ActivateControllerWorld(token);}
  __finally {if(!activated)RetireControllerWorld(token);LeaveInvocation();}return result;
}
uint32_t __cdecl ObserveControllerDestroy(void *raw){
  auto original=ControllerDestroy();if(!original)return 0;bool available=false;
  if(!EnterInvocation(&available))return original(raw);uint32_t result=0;
  __try {if(available)RetireCurrentControllerWorld(raw);result=original(raw);}
  __finally {LeaveInvocation();}return result;
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryWorldControllerCreateHook(){
  __asm {
    push dword ptr [esp+8]
    push dword ptr [esp+8]
    push ecx
    call ObserveControllerCreate
    add esp,12
    ret 8
  }
}
extern "C" __declspec(naked) void __cdecl TFUHistoryWorldControllerDestroyHook(){
  __asm {
    push ecx
    call ObserveControllerDestroy
    add esp,4
    ret
  }
}

extern "C" int __stdcall TFUHistoryCaptureControllerWorld(const TFUHistoryToken *owner,uint64_t step){
  if(!owner||!owner->owner||owner->reserved||!step)return TFU_HISTORY_BAD_ARGUMENT;
  if(preparationDepth)return TFU_HISTORY_REJECTED;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;
    if(activePreparations)return TFU_HISTORY_REJECTED;
    if(!ValidSerial(owner->serial))return TFU_HISTORY_REJECTED;
    auto raw=reinterpret_cast<void *>(owner->owner);
    if(!registry->IsWorldOwner(Token(raw,owner->serial)))return TFU_HISTORY_REJECTED;
    return CaptureControllerWorldLocked(raw,step,true,true)?TFU_HISTORY_OK:TFU_HISTORY_REJECTED;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}

extern "C" int __stdcall TFUHistoryCaptureCompletedControllers(uint64_t step,uint32_t *captured,uint32_t *rejected){
  if(!step||!captured||!rejected||captured==rejected)return TFU_HISTORY_BAD_ARGUMENT;
  if(preparationDepth)return TFU_HISTORY_REJECTED;
  try {Lock lock;if(!registry||closing)return TFU_HISTORY_NOT_INITIALIZED;
    if(activePreparations)return TFU_HISTORY_REJECTED;
    const auto controllers=registry->NativeControllers();uint32_t accepted=0,skipped=0;
    for(const auto &owner:controllers){if(CaptureControllerWorldLocked(owner.owner,step,true))++accepted;else ++skipped;}
    *captured=accepted;*rejected=skipped;return TFU_HISTORY_OK;
  }catch(...){return TFU_HISTORY_INTERNAL_ERROR;}
}
extern "C" int __stdcall TFUHistorySetWorldPrepareFunction(TFUHistoryNativePrepare prepare){
  if(!prepare||reinterpret_cast<void *>(prepare)==reinterpret_cast<void *>(TFUHistoryWorldPrepareHook))return TFU_HISTORY_BAD_ARGUMENT;
  Lock lock;if(activeInvocations||(nativeWorldPrepare&&nativeWorldPrepare!=prepare))return TFU_HISTORY_REJECTED;
  nativeWorldPrepare=prepare;return TFU_HISTORY_OK;
}
namespace {
TFUHistoryNativePrepare WorldPrepareFunction(){Lock lock;return nativeWorldPrepare;}
void BeginWorldPreparation(bool nested,uint64_t step,uint32_t captureSession,bool available){
  Lock lock;
  if(!activePreparations){preparationStep=step;preparationSession=captureSession;preparationFailed=!available;}
  else if(!nested&&(step!=preparationStep||captureSession!=preparationSession||!available))preparationFailed=true;
  ++activePreparations;
}
void EndWorldPreparation(bool succeeded){
  try {Lock lock;if(!succeeded)preparationFailed=true;
    if(--activePreparations||preparationFailed||!registry||closing||!preparationStep||preparationSession!=session)return;
    const auto controllers=registry->NativeControllers();
    for(const auto &owner:controllers)CaptureControllerWorldLocked(owner.owner,preparationStep,true);
  }catch(...){/* Native preparation success must not depend on cache allocation. */}
}
uint32_t __cdecl ObserveWorldPrepare(void *context){
  auto original=WorldPrepareFunction();if(!original)return 0;bool available=false;
  if(!EnterInvocation(&available))return original(context);
  const ULONG previousDepth=preparationDepth++;uint32_t result=0;bool succeeded=false;
  BeginWorldPreparation(previousDepth!=0,worldCaptureStep,worldCaptureSession,available);
  __try {result=original(context);succeeded=true;}
  __finally {EndWorldPreparation(succeeded);--preparationDepth;LeaveInvocation();}return result;
}
}
extern "C" __declspec(naked) void __cdecl TFUHistoryWorldPrepareHook(){
  __asm {
    push ecx
    call ObserveWorldPrepare
    add esp,4
    ret
  }
}
