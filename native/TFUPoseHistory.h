#pragma once
#include <stdint.h>
#include <windows.h>
#define TFU_POSE_HISTORY_ABI 0x00010014u
enum TFUHistoryStatus {TFU_HISTORY_OK=0,TFU_HISTORY_NOT_INITIALIZED=1,
  TFU_HISTORY_REJECTED=2,TFU_HISTORY_BAD_ARGUMENT=3,
  TFU_HISTORY_INTERNAL_ERROR=4,TFU_HISTORY_EPOCH_EXHAUSTED=5};
#pragma pack(push,4)
struct TFUHistoryToken {uint32_t owner;uint32_t reserved;uint64_t serial;};
struct TFUWorldToken {uint32_t geometry,base,joints,reserved;TFUHistoryToken owner;uint64_t binding;};
struct TFUCameraToken {uint32_t camera,reserved;uint64_t generation,epoch;};
struct TFUClockScope {uint64_t epoch,id;};
struct TFUClockStatus {uint32_t bytes,phase,foreignJobs,childJobs,runningJobs,envelopes,submitters,callbackContexts,rootContexts,accepting,uncertain,failed,quarantined,activeCalls;};
#pragma pack(pop)
static_assert(sizeof(TFUHistoryToken)==16,"Versioned x86 token ABI");
static_assert(sizeof(TFUWorldToken)==40,"Versioned x86 world binding ABI");
static_assert(sizeof(TFUCameraToken)==24,"Versioned x86 camera ABI");
static_assert(sizeof(TFUClockScope)==16&&sizeof(TFUClockStatus)==56,"Versioned x86 clock backend ABI");
typedef void (__cdecl *TFUHistoryClockCallback)(void *,int32_t);
typedef uint32_t (__cdecl *TFUHistoryNativeClockSubmit)(TFUHistoryClockCallback,void *,int32_t,uint32_t);
typedef void (__thiscall *TFUHistoryNativeCameraSetter)(void *,const float *);
typedef uint32_t (__thiscall *TFUHistoryNativeCameraRoutine)(void *);
typedef void (__cdecl *TFUHistoryBasisBuilder)(float *,float *,float *,const float *);
typedef void (__thiscall *TFUHistoryNativeDraw)(void *,void *,uint32_t);
typedef void (__thiscall *TFUHistoryNativeWorldDraw)(void *,void *,void *,void *,void *);
typedef void (__thiscall *TFUHistoryNativeWorldPublish)(void *,void *);
typedef uint32_t (__thiscall *TFUHistoryNativeWorldAssign)(void *,void *);
typedef uint32_t (__thiscall *TFUHistoryNativeWorldClear)(void *);
typedef uint32_t (__thiscall *TFUHistoryNativeWorldCreate)(void *,uint32_t,const char *);
typedef uint32_t (__thiscall *TFUHistoryNativePrepare)(void *);
typedef void (__thiscall *TFUHistoryNativeInitialize)(void *,void *,void *);
typedef void (__thiscall *TFUHistoryNativeRelease)(void *);
typedef void (__thiscall *TFUHistoryNativeBind)(void *,const float *);
typedef void (__thiscall *TFUHistoryNativeBatch)(void *,void *,int32_t,int32_t);
#pragma pack(push,4)
struct TFUPaletteOutcome {uint32_t preparationStatus;uint32_t overrideUsed;};
struct TFUPaletteCall {
  uint32_t bytes;void *context;void *parameters;uint32_t argument;
  void *geometry;const float *base;uint32_t joints;float alpha;
  const uint8_t *remap;uint32_t drawJoints;uint32_t firstRegister;
  TFUHistoryBasisBuilder basisBuilder;TFUHistoryNativeDraw nativeDraw;TFUPaletteOutcome *outcome;
};
#pragma pack(pop)
static_assert(sizeof(TFUPaletteCall)==56&&sizeof(TFUPaletteOutcome)==8,"Versioned x86 draw ABI");
// Initialize explicitly after loading. Stop hook callers, Shutdown, then unload.
// Capture requires a stable completed pose, not merely a registry lock.
extern "C" {
uint32_t __stdcall TFUHistoryABI();
int __stdcall TFUHistoryInitialize();
int __stdcall TFUHistoryShutdown();
// Explicit backend only; no IAT installation or native clock writes. Rejected
// admission leaves result unchanged and MUST NOT be treated as native handle0.
// Stop callers and join actual native jobs before Shutdown/module unload.
int __stdcall TFUHistorySetClockSubmitFunction(TFUHistoryNativeClockSubmit original);
int __stdcall TFUHistorySubmitClockJob(TFUHistoryClockCallback callback,void *state,int32_t argument,uint32_t label,uint32_t *result);
int __stdcall TFUHistoryAcquireClockScope(uint32_t timeout,TFUClockScope *output);
int __stdcall TFUHistoryInvokeClockScope(const TFUClockScope *scope,TFUHistoryClockCallback body,void *context,int32_t argument);
int __stdcall TFUHistoryDrainClockScope(const TFUClockScope *scope,uint32_t timeout);
int __stdcall TFUHistoryReleaseClockScopeAfterRestore(const TFUClockScope *scope);
int __stdcall TFUHistoryCloseClockJobs();
// After close, native scheduler/state/clock/history reconciliation and real job
// joins: rebuild backend with a fresh epoch. Does not undo native partial state.
int __stdcall TFUHistoryReconcileClockBackend();
int __stdcall TFUHistoryClockStatus(TFUClockStatus *output); // output.bytes=sizeof.
// Explicit camera lifetime. Caller owns native storage/fence and retires before
// release. Present rebuilds private0x220-byte output; original math runs unlocked.
int __stdcall TFUHistoryBeginCamera(void *camera,TFUCameraToken *output);
int __stdcall TFUHistoryRetireCamera(const TFUCameraToken *token);
int __stdcall TFUHistoryCaptureCamera(const TFUCameraToken *token,uint64_t step,const void *state,uint32_t cut);
int __stdcall TFUHistoryPresentCamera(const TFUCameraToken *token,float alpha,const void *observed,void *output,TFUHistoryNativeCameraSetter original);
// Immutable original B51F40 setter. Raw getter hook is specific to the two
// audited 420180 call sites: ECX=source state, EBX=render view, EAX=source/RET.
// Requires an active SetRenderPhase and explicit lifetime/capture of source-4950.
// Caller fences source/destination storage. Failure retains the native copied
// view. Stop callers before Shutdown/unload; never install at arbitrary getters.
int __stdcall TFUHistorySetCameraSetter(TFUHistoryNativeCameraSetter original);
void __cdecl TFUHistoryCameraCopyHook();
// Ordinary SWGameCamera only: original755960 constructor,74DC10 destructor,
// and458EE0 update. Configure original trampolines before installing hooks.
int __stdcall TFUHistorySetCameraFunctions(TFUHistoryNativeCameraRoutine create,TFUHistoryNativeCameraRoutine destroy,TFUHistoryNativeCameraRoutine update);
void __cdecl TFUHistoryCameraCreateHook(); // Raw camera ECX / EAX / RET.
void __cdecl TFUHistoryCameraDestroyHook();
void __cdecl TFUHistoryCameraUpdateHook();
int __stdcall TFUHistoryCameraToken(void *camera,TFUCameraToken *output);
// Thread/session-local completed step to capture AFTER a successful native
// update returns; snapshot on entry. Step0 disables. Cut resets interpolation.
// Driver must supply this from the fixed simulation clock and clear on exit.
int __stdcall TFUHistorySetCameraCaptureStep(uint64_t step,uint32_t cut);
int __stdcall TFUHistoryBegin(void *owner,TFUHistoryToken *token);
int __stdcall TFUHistoryActivate(void *owner,uint64_t serial,const float *base,uint32_t joints);
// Explicit non-skeletal lifetime/binding. Pose APIs remain unavailable.
int __stdcall TFUHistoryActivateWorld(void *owner,uint64_t serial);
int __stdcall TFUHistoryBindWorld(void *geometry,void *owner,uint64_t serial);
int __stdcall TFUHistorySetWorldGeometryFunctions(TFUHistoryNativeWorldAssign assign,TFUHistoryNativeWorldClear clear);
// Raw controller ECX + geometry / RET4, and controller ECX / RET.
void __cdecl TFUHistoryWorldGeometryAssignHook();
void __cdecl TFUHistoryWorldGeometryClearHook();
int __stdcall TFUHistorySetWorldControllerFunctions(TFUHistoryNativeWorldCreate create,TFUHistoryNativeWorldClear destroy);
// Raw controller ECX + handle/name / RET8, and controller ECX / RET.
void __cdecl TFUHistoryWorldControllerCreateHook();
void __cdecl TFUHistoryWorldControllerDestroyHook();
int __stdcall TFUHistoryCapture(void *owner,uint64_t serial,const float *base,uint32_t joints);
int __stdcall TFUHistoryBind(void *geometry,void *owner,uint64_t serial,const float *base,uint32_t joints);
int __stdcall TFUHistorySample(void *geometry,const float *base,uint32_t joints,float alpha,float *output);
// Capture stable completed instance transforms with simulation step IDs. Tokens
// include module session and geometry binding generation; rejected samples do
// not modify output. These exports do not install automatic capture/draw hooks.
int __stdcall TFUHistoryWorldBinding(void *geometry,const float *base,uint32_t joints,TFUWorldToken *output);
int __stdcall TFUHistoryCaptureWorld(const TFUWorldToken *token,uint64_t step,const float *matrix,uint32_t discontinuity);
// Caller must hold stable controller/geometry storage AFTER the native worker
// or full-frame completion fence. Validates lifetime before reading controller.
int __stdcall TFUHistoryCaptureControllerWorld(const TFUHistoryToken *owner,uint64_t step);
// Same stable-storage/fence requirement; selects only native-create-hook owners.
int __stdcall TFUHistoryCaptureCompletedControllers(uint64_t step,uint32_t *captured,uint32_t *rejected);
int __stdcall TFUHistorySetWorldPrepareFunction(TFUHistoryNativePrepare prepare);
void __cdecl TFUHistoryWorldPrepareHook(); // Raw ECX preparation context / RET.
int __stdcall TFUHistorySampleWorld(const TFUWorldToken *token,float alpha,float *output);
int __stdcall TFUHistoryForgetGeometry(void *geometry);
int __stdcall TFUHistoryRetire(void *owner,uint64_t serial);
// basisBuilder must be the validated, non-reentrant native quaternion helper.
// Output is committed only after the entire remapped affine palette is ready.
int __stdcall TFUHistoryPalette(void *geometry,const float *base,uint32_t joints,float alpha,
    const uint8_t *remap,uint32_t drawJoints,TFUHistoryBasisBuilder basisBuilder,
    float *output,uint32_t capacityFloats);
// The upload export replaces only the validated native method-load call site,
// never the device vtable. Native draw callbacks run without the module lock.
int32_t __stdcall TFUHistoryUpload(void *device,uint32_t first,const float *data,uint32_t count);
int __stdcall TFUHistoryInvokePalette(const TFUPaletteCall *call);
int __stdcall TFUHistoryOwnerToken(void *owner,TFUHistoryToken *token);
// Set validated unhooked originals/trampolines before installing either raw
// x86 hook. They remain configured across registry shutdown for pass-through.
int __stdcall TFUHistorySetOwnerFunctions(TFUHistoryNativeInitialize initialize,TFUHistoryNativeRelease release);
// Raw native-ABI entry targets, not ordinary C-callable functions. Init uses
// ECX owner + two stack args / RET 8; release uses ECX owner / RET.
void __cdecl TFUHistoryOwnerInitHook();
void __cdecl TFUHistoryOwnerReleaseHook();
int __stdcall TFUHistorySetBindingFunction(TFUHistoryNativeBind bind);
// Raw ECX geometry + one pose-base stack arg / RET 4 native entry target.
void __cdecl TFUHistoryGeometryBindHook();
int __stdcall TFUHistorySetBatchFunction(TFUHistoryNativeBatch batch);
// Raw ECX manager + entries/count/phase stack args / RET 12 entry target.
void __cdecl TFUHistoryAnimationBatchHook();
int __stdcall TFUHistorySetGeometryCleanupFunction(TFUHistoryNativeRelease cleanup);
// Raw ECX geometry / RET native cleanup entry target.
void __cdecl TFUHistoryGeometryCleanupHook();
int __stdcall TFUHistorySetDrawFunctions(TFUHistoryNativeDraw draw,TFUHistoryBasisBuilder basisBuilder);
// Per-thread render phase, invalidated by registry restart. Disabled by default.
int __stdcall TFUHistorySetRenderPhase(uint32_t enabled,float alpha);
// Raw ECX context + parameters/argument stack args / RET 8 native entry.
void __cdecl TFUHistoryDrawHook();
int __stdcall TFUHistorySetWorldDrawFunction(TFUHistoryNativeWorldDraw draw);
// Raw B96D60 ABI: ECX command + device/descriptor/parameters/extras, RET16.
// Same explicit per-thread render phase as the pose hook; disabled by default.
void __cdecl TFUHistoryWorldDrawHook();
int __stdcall TFUHistorySetWorldPublishFunction(TFUHistoryNativeWorldPublish publish);
// Explicit completed simulation step for this thread/session; 0 disables.
// Caller must own a completed-step publication scope and clear it afterwards.
int __stdcall TFUHistorySetWorldCaptureStep(uint64_t step);
// Raw BF5C60 ABI: ECX controller + owner stack argument, RET4. Original first.
void __cdecl TFUHistoryWorldPublishHook();
}
