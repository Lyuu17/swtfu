# Native pose-history module

`build/TFUPoseHistory.dll` is the x86 native cache component for the ongoing
60 FPS work. It installs no hooks and changes no frame-rate or physics settings.
The current Python launcher does not load it.

Build from the workspace with `powershell -File native/build.ps1`. The script
uses the installed Visual Studio 2022 x86 compiler and Windows SDK. The DLL uses
a static C++ runtime; its only imported DLL is `KERNEL32.dll`.

The owned native harness loads it through an absolute path, resolves the sixty-seven
undecorated exports, checks ABI version `0x00010014`, exercises the API, shuts it
down and unloads it. Run the existing `analysis/build_scene_harness.ps1`, then
`analysis/TestDmmScene.exe --native-cloth` and
`python analysis/verify_pose_module.py` to reproduce that check.

The lifecycle contract is:

1. Call `TFUHistoryInitialize` after loading, outside loader callbacks.
2. Before pose-owner initialization/reinitialization, call `TFUHistoryBegin`.
   Activate its returned token only after original initialization succeeds.
3. Bind geometry only after the original geometry binding succeeds, with the
   matching owner token, pose base and joint count.
4. Capture only stable completed simulation poses after the native animation
   fence. The module lock does not synchronize game simulation writers.
5. Sample into caller-owned render storage. A rejected lookup must use the
   original rendering path. Forget geometry on cleanup and retire an owner
   before its original pose storage is released.
6. Shutdown the registry, stop/join export callers, then unload the DLL.

Registry exports serialize access. Shutdown rejects new history work while
waiting for active draw invocations; subsequent valid cache calls report `TFU_HISTORY_NOT_INITIALIZED`. Reinitializing the registry
within the loaded DLL changes the session encoded in tokens, so an earlier
session's token cannot match a newly created record. Tokens expire when the
DLL is unloaded. Token layout and status values are defined in `TFUPoseHistory.h`;
no C++ objects or allocation ownership cross the API boundary. Cache/preparation
exports translate C++ failures to status values; native draw exceptions retain
their original propagation after scope cleanup. Callers supply
valid input/output memory and retain source storage throughout capture.

Native loading and API tests do not establish game-hook coverage, allocator
failure recovery, live performance, GPU rendering or full physics equivalence.
Palette preparation is now exported as `TFUHistoryPalette`; the DLL now also owns the scoped upload adapter. Game lifecycle/call-site
hook installation is still outside the DLL.


`TFUHistoryPalette` samples the registered pose history, applies mesh byte remaps,
uses the caller-supplied validated native quaternion-basis helper, and builds
three affine float4 rows per joint. It commits caller output only after the
entire palette succeeds. The helper must be pure and must not re-enter module
exports: export serialization remains held during its execution. The API
rejects invalid capacities, remaps, counts and alpha, and unavailable history;
the caller retains the original native upload on rejection.

Ten owned DLL-to-original-draw cases check interpolation endpoints/midpoint,
a remapped subset, a nontrivial rotation, and fallback for missing or retired
bindings, mismatched base, short capacity and invalid alpha. They preserve the
original context/geometry and observe the device endpoint; no real GPU is used.


`TFUHistoryInvokePalette` accepts a packed56-byte x86 request describing the
original native draw, original context/parameters/argument, history lookup and
constant destination. It prepares private constant storage and invokes the
original draw without holding the registry lock. `TFUHistoryUpload` replaces
only the validated native method-load call site, never the COM vtable. It
forwards the original method call, substituting data once only when the scoped
register/count match. Missing history, unmatched requests and an uninitialized
registry retain original rendering. Do not pass a hooked/reentrant entry as the
original draw function.

A finally boundary restores thread scope, frees private constant storage and
releases the active-invocation count on normal return or native exception.
Shutdown from within that scope is rejected. A controller-thread shutdown waits
for active invocations to finish; it does not abort native draws. Stop new hook
entries before shutdown and remove/quiesce ALL upload and draw callbacks before
unloading. The drain is not permission to unload while another caller can still
enter an export. Eleven owned tests include a held original draw and a waiting
shutdown, alongside fallback, nested native draw and original exception checks.
Real game-hook installation, device/GPU behavior and complete worker coverage
remain unverified.


ABI1.3 adds the first raw native entry hooks: `TFUHistoryOwnerInitHook` and
`TFUHistoryOwnerReleaseHook`. These are x86 ECX/stack-ABI jump targets, NOT
ordinary C-callable exports. Configure validated unhooked original functions or
trampolines with `TFUHistorySetOwnerFunctions` BEFORE installing either entry
jump. Configuration is immutable except for an idempotent repeat; registry
shutdown preserves it so installed hooks continue original pass-through.

Initialization begins a cache generation before the original initializer,
activates it only after the original returns with usable storage, and retires
an incomplete generation if the original raises. Release invalidates the owner
before calling the original destructor helper. `TFUHistoryOwnerToken` retrieves
an active automatically registered owner token for subsequent capture/binding.
The common invocation drain also covers these lifecycle callbacks and rejects
same-thread shutdown. Caller-owned trampolines must remain alive until hooks
are removed and every caller has quiesced.

Nine owned-process cases install verified five-byte jumps at copied native
6D4F20/6D51A0 entries, using relocation-free prologue trampolines. They test
in-place native reinitialization with identical addresses, cache invalidation
at the free endpoint, inactive-registry pass-through, allocation-exception
cleanup, and restoration of both entries before freeing trampolines. The real
allocator endpoints remain observed. No game process is patched; atomic live
installation, all native lifecycle paths, binding/capture hooks and full FPS
policy remain unfinished.


ABI1.4 adds `TFUHistoryGeometryBindHook` and its immutable original-function
configuration. The raw hook uses ECX geometry and one pose-base stack argument
with RET4. Configure a validated unhooked binder/trampoline before installing
its entry jump. The observer forgets any earlier geometry association, invokes
the original binder, preserves its root/skeleton/count guards, and associates
history only when exactly one active registered owner matches the resulting
pose base and count. It does not infer ownership from caller registers.

The owned harness now patches/restores all three copied native entries. Manual
`TFUHistoryBind` calls were removed from the connected lifecycle fixture; the
original B26780 call now associates history automatically. Nine additional
binding cases cover missing roots, zero-count guard with an inaccessible skin
pointer, count mismatch, deliberately ambiguous registered owners, unknown
pose storage and subsequent rebind. The helper never dereferences skin after
an original skeleton/count guard rejects the path. Game process installation,
geometry-destruction coverage, batch-capture hooks and full FPS policy remain
unfinished.


ABI1.5 adds `TFUHistorySetBatchFunction` and the raw
`TFUHistoryAnimationBatchHook` (ECX manager, three stack arguments, RET12).
Configure a validated unhooked original/trampoline before installing the entry.
The original batch runs first. Only completed phase-2 entries with zero pending
dependencies and actor state 7 can supply registered-owner history. The done
field is a byte; adjacent padding is ignored. Shared owners are captured once
per batch invocation. Invalid registered storage retires history for fallback.
The common invocation drain covers the original batch and capture observer.

The owned harness now installs/restores four copied entries. Three additional
cases hold a native queued writer, check unpublished/previous history during
the hold, then check automatic capture after native queue drain. They cover
same-owner duplicate entries, nonzero done-field padding and unsupported-phase
preservation. Run `analysis/verify_pose_capture_hooks.py` after the native run.
Per-frame association across multiple batches, all actor states, geometry
cleanup, live installation and the complete 30 Hz simulation/60 Hz presentation
policy remain unfinished. Earlier ABI sections describe their historical scope.


ABI1.6 adds `TFUHistorySetGeometryCleanupFunction` and the raw
`TFUHistoryGeometryCleanupHook` (ECX geometry, plain RET). Configure a validated
unhooked original/trampoline before installing the entry. The observer forgets
the association before original cleanup; native skin pointer clearing alone
would leave the geometry's stale pose field. The original runs outside the
registry lock, with the common invocation drain and finally protection.

Twelve owned native samples cover pre-free invalidation, stale pose fields,
same-address original rebind and cleanup exception recovery. The copied B28050
entry and two observed free call sites are restored before trampoline disposal.
Run `analysis/verify_pose_cleanup_hooks.py` after the native harness. This uses
an empty ordinary skin pool and no child objects; child destructors, nonempty
pool compaction and alternate pools remain unverified. Five distinct copied
native entry hooks are now exercised across the connected lifecycle tests.
The launcher still has no module loader and no game process was patched.


ABI1.7 adds `TFUHistorySetDrawFunctions`, `TFUHistorySetRenderPhase` and raw
`TFUHistoryDrawHook` (ECX context, parameters/argument stack args, RET8).
Configure validated unhooked draw and quaternion functions before patching.
The hook extracts the original native descriptors and routes through the
existing private palette/upload scope. It uses skin+98 as the observed read
base, querying joint count from a matching registered geometry association;
an unregistered or different selected pose base retains native rendering.

Render phase is explicit, per thread, disabled by default and registry-session
tagged. Enable it only for the presentation scope and disable it afterward.
Invalid phase requests preserve prior state. No clock or simulation scheduler
is installed by this API. Nested draw entries retain native fallback. Fourteen
owned entry-hook cases verify reconstructed palettes, phase endpoints/midpoint,
missing-data guards, selected-base mismatch, nested/thread isolation and native
exception recovery. Run `analysis/verify_pose_draw_hooks.py` after the harness.
Session invalidation is implemented but not separately exercised here. Real
GPU/device execution, game installation and full 30 Hz simulation/60 Hz render
cadence remain unfinished. No production launcher or borderless change.


ABI1.8 adds session-aware world-history exports, without installing world hooks:
`TFUHistoryWorldBinding`, `TFUHistoryCaptureWorld`, and `TFUHistorySampleWorld`.
The packed 40-byte `TFUWorldToken` includes geometry/base/joint identity, owner
session/generation, and a 64-bit binding generation. Obtain it only after a
successful owner activation and geometry bind. Capture stable completed instance
matrices with simulation step IDs; capture does not synchronize engine writes.
Rebinding invalidates prior world tokens even for the same owner/base. A module
restart rejects old tokens even when addresses and internal generations repeat.
Sample failure leaves the destination untouched. Reserved token fields must be0.
The exported cache supports translation/positive-scale/shortest-arc rotation;
shear, reflections, projective and degenerate transforms are rejected.

The owned module fixture verifies18 world API cases, token/output guards,
rebind/cleanup/owner reuse/session restart, and rejection of a stale token before
reading a deliberately inaccessible matrix pointer. A concurrent reader observes
whole old/new matrices, then receives NOT_INITIALIZED on shutdown and joins before
unload. No automatic game transform capture or private-world draw hook is installed
by these exports; the current launcher continues to leave the DLL unloaded.

A future world draw hook must also match the observed instance world matrix to
the history's current endpoint before overriding it. Native skin publication can
update only a subset of instance packets; binding metadata alone does not prove
that every instance has received the newest geometry transform.


ABI1.9 adds `TFUHistorySetWorldDrawFunction` and raw `TFUHistoryWorldDrawHook`
(ECX command, four stack arguments, RET16). Configure the validated unhooked
B96D60 original/trampoline before patching an entry. The hook shares the explicit
per-thread, session-tagged render phase; default/unknown/stale/unsupported paths
call the native original. It matches the observed instance matrix to the
binding history's current endpoint under the registry lock before sampling.
Native/GPU callbacks run after releasing module/registry locks. Supported draws
use private instance/command/combined-cache storage for all three shader matrices.

Nested draws use native parameters. If a nested draw interrupts an interpolated
outer upload, the outer call restores a complete native parameter set before
returning, avoiding a mixture of native and interpolated constants. B96D60 is
parameter preparation; this restoration does not draw geometry twice. Native
exceptions propagate while finally blocks release invocation/nesting state.
Shutdown drains world invocations with the common module invocation counter.
Stop entry callers and restore hooked entries before freeing trampolines/unloading.

The harness patches only its owned copy of B96D60. Twenty cases/64 uploads cover
phase endpoints/midpoint, instance mismatch, unbound/mismatched metadata, special
cached/material paths, thread and session scope, nested restoration, native SEH,
post-exception recovery, shutdown/restart and rebind. The game launcher does not
load or install this hook, and automatic completed-world capture remains pending.


ABI1.10 (`0x0001000A`,34 exports) adds the configured original/publication hook
for BF5C60 and `TFUHistorySetWorldCaptureStep(uint64_t)`. Capture step0 disables
observation. The scope is per thread and module session, defaults to disabled,
and must be supplied by a completed simulation-step publication scope, then
cleared by the caller. The renderer stamp3864CC8 is not used as a step ID.
The raw hook snapshots that scope on entry, calls the original first, then
captures the completed geometry skin matrix through the active binding. Native
exceptions skip capture and propagate with invocation accounting restored.
Reentrant callbacks may configure a future scope without relabeling the active
publication. Registry/cache failure preserves completed native publication.

The owned harness hooks its copied BF5C60 and connects post-publication capture
to the world draw hook without any manual world-capture API calls.24 cases/72
uploads cover duplicate/conflicting steps, disabled scope, default worker thread,
64-bit IDs, gaps, cleanup/rebind, missing/changed base, native failure/recovery,
shutdown and stale-session scope. The callback reconfigures a scope during the
native original to verify the entry snapshot remains authoritative. Bounds
endpoints are observed; this is not a full live game-world test. Actual frame
scheduler integration, non-skinned object coverage and live validation remain
unfinished. The Python launcher still leaves this module unloaded.


ABI1.11 (`0x0001000B`,36 exports) adds `TFUHistoryActivateWorld(owner,serial)` and `TFUHistoryBindWorld(geometry,owner,serial)` for explicit non-skeletal lifetimes. Query their world token with null base/zero joints; existing CaptureWorld/SampleWorld use it with completed simulation steps. Pose APIs still require a valid pose base and positive joint count. Mixed null/nonzero shapes reject. Rebind, cleanup, retirement and module-session restart invalidate old tokens. The raw world draw hook also accepts explicit non-skeletal bindings without skin metadata and requires a current-instance match. The publication hook also captures explicit non-skeletal bindings from a published skin-node world matrix without pose data. Automatic lifecycle registration and alternative worker/no-skin publication remain unfinished.

`TFUHistoryBindWorld` rejects a geometry that already has a skeletal binding and preserves its exact identity. Explicit non-skeletal registration cannot displace an animation binding; valid world-only rebinds still invalidate older world-only tokens.


ABI1.12 (`0x0001000C`,39 exports) adds `TFUHistorySetWorldGeometryFunctions(assign,clear)` and raw `TFUHistoryWorldGeometryAssignHook`/`TFUHistoryWorldGeometryClearHook`. Configure validated unhooked BF42B0/BF44A0 equivalents before invoking raw hooks. Assignment uses controller ECX plus geometry/RET4; clear uses controller ECX/RET. The original EAX result is preserved. A controller must already have an explicit active world-only owner token. The hook binds successful assignments, keeps identical assignments/history, and invalidates only that controller's own world-only binding before native replacement/release. Unrelated bindings are retained. Native callbacks run outside cache locks; exceptions propagate and invocation accounting unwinds. Constructor/destructor registration and the destructor's separate inlined release path are not wired automatically. No launcher loads or installs these hooks.


ABI1.13 (`0x0001000D`,42 exports) adds `TFUHistorySetWorldControllerFunctions(create,destroy)` and raw controller create/destroy hooks. The constructor uses controller ECX plus full handle/name and RET8; the destructor uses controller ECX/RET. Creation begins a fresh registry generation before native initialization, activates world-only history after native success, and removes incomplete cache owners on failure. Destruction retires the controller owner and all its bindings before the native destructor, including its separate inlined geometry-release route. Native EAX, exceptions, list mutations and incomplete failure state are preserved. Together with geometry assignment hooks, the owned fixture registers controller/geometry lifetimes without explicit Begin/ActivateWorld/BindWorld calls. The Python launcher still does not install these hooks; live scheduler/frame/GPU verification remains unfinished.

The publication hook also reconciles an active controller's actual no-pose geometry without relying on a setter call. Stable geometry retains history; replacement/null geometry invalidates old controller bindings. Unrelated target bindings are preserved. This lifecycle reconciliation still runs when matrix capture is disabled; step0 continues to disable matrix capture itself. Alternate worker/no-skin and full-frame coverage remain unfinished.


ABI1.14 (`0x0001000E`,43 exports) adds `TFUHistoryCaptureControllerWorld(ownerToken,completedStep)`. Call it only after the native worker/full-frame fence while controller/geometry storage is stable. It validates module session and active world-only owner generation before reading controller memory, reconciles current geometry and captures its canonical completed world matrix. It cannot capture another owner's skeletal/world binding. It does not wait for engine jobs or derive step IDs. The owned alternate-world queue now calls it after original6B9F80 returns, without manual geometry binding or caller-supplied matrix pointers. Live final-frame integration remains unfinished.


ABI1.15 (`0x0001000F`,44 exports) adds `TFUHistoryCaptureCompletedControllers(step,captured,rejected)`. After a caller-held engine completion fence, it snapshots only active records tagged by the successful native controller-create hook. Explicit opaque world-owner keys are excluded. Supported owned no-pose canonical world matrices capture through existing lifetime/reconciliation logic; missing geometry, pose-owned targets and other unsupported cases are counted as rejected. Counts use distinct guarded output pointers. This API neither waits nor derives steps, and it is not yet installed at the full game-frame endpoint.


ABI1.16 (`0x00010010`,46 exports) adds immutable `TFUHistorySetWorldPrepareFunction(original)` and raw ECX/RET `TFUHistoryWorldPrepareHook`. The hook snapshots the caller-configured completed-step/session, runs the original preparation without a cache lock, and captures live registered controllers only after successful outermost return. Publication calls while any preparation scope is active reconcile lifetimes but defer matrix capture; explicit capture APIs reject across threads during that scope. Native EAX and exceptions are preserved. Completed-controller batch capture now also resolves registered skeletal bindings from each known controller's current geometry; explicit token-based controller capture remains ownership-restricted. The controlled scope fixture observes its preparation/producer callbacks and does not execute original423480. Real full-frame installation and step mapping remain unfinished.


Preparation activity is now shared across threads. Final capture waits for the last successful scope exit; conflicting concurrent outer step/session scopes or failed scopes conservatively skip capture. A held-preparation fixture verifies foreign-thread publication and explicit batch capture cannot commit early. Multiple overlapping preparation scopes and the original full engine endpoint remain unverified.

The owned harness now also runs original423480 control flow/bookkeeping with44 service callees and job/catalog/virtual endpoints observed. `verify_native_full_prepare.py` verifies the single/double update paths, post-wait callback, native EAX/SEH cleanup and capture after original return. Actual scheduler/asset/physics services and original exception paths remain unexecuted by that fixture; it is not live-game equivalence evidence.

ABI1.17 (`0x00010011`,50 exports) adds explicit camera begin, retire, capture and present APIs. Presentation snapshots history, rebuilds private camera state outside the registry lock, and revalidates lifetime and revision before committing. Active invocations pin registry storage through native exceptions; source storage remains caller-owned. Native camera lifecycle hooks and live installation remain unfinished.

ABI1.18 (`0x00010012`,52 exports) adds immutable `TFUHistorySetCameraSetter(originalB51F40)` and raw `TFUHistoryCameraCopyHook`. The hook replaces only the two audited B51750 calls in original420180, after its native0x220-byte copy; ECX is the selected camera-state pointer and EBX the render view. It resolves an explicitly registered camera at source-0x4950, requires the current thread/session render phase, rebuilds private state, and commits renderView+0x20 after lifetime/revision revalidation. EAX returns the original source; flags, other GP registers, x87/XMM/MXCSR state are restored. Recursive calls and unavailable/stale/mismatched/faulting histories retain the native copied state. Native storage fences and lifecycle/capture hooks remain caller responsibilities; this does not install a game patch.

The owned camera replay now executes original420180 through the DLL hook on both player/fallback routes. Twenty-four cases cover interpolation, phase disable/restart/thread isolation, source mismatch, retirement, reentrant shutdown, native rebuild faults and recursive getter suppression, with independent complete-state matrix checks. Rebuild using `analysis/build_camera_rebuild_probe.ps1`; these checks do not establish live camera behavior or full physics equivalence.

ABI1.19 (`0x00010013`,58 exports) adds immutable `TFUHistorySetCameraFunctions(create,destroy,update)`, raw ECX/RET `CameraCreateHook`, `CameraDestroyHook`, `CameraUpdateHook` exports (with the TFUHistory prefix), `TFUHistoryCameraToken` and thread/session-local `TFUHistorySetCameraCaptureStep(step,cut)`. Originals are the ordinary755960 constructor,74DC10 destructor and458EE0 update; configure trampolines before installing. Construction invalidates previous identity before the original call and registers only after a successful self return. Destruction retires before original cleanup. Original returns and exceptions propagate, with invocation draining on every exit. Update snapshots token/step/cut on entry, captures after successful outermost completion, and rejects a replaced lifetime. Step0 disables capture; the fixed simulation driver must set and clear the completed step. Native storage lifetime remains caller-fenced.

Camera capture first snapshots native0x220 bytes without cache locks, then submits private storage. This prevents a source read fault from escaping across ordinary C++ lock destructors. Seventeen owned lifecycle/update observations cover failed construction, constructor/update/destructor faults, nested updates, replacement during update, cuts, entry-step snapshots, stale session configuration and source read failure. Sixteen subsequent original420180 publications use this automatic registration/capture path without explicit Begin/Capture calls. The actual constructor/destructor/update bodies are observed callbacks in this replay; live fixed-step driver integration and installation remain unfinished.

`FixedStepDriver.h` owns accepted-frame planning and commits cadence accounting only after the native update callback returns. Non-step frames retain time as carry; paused frames run zero-dt maintenance without a positive step. A pending frame cannot be replaced or executed recursively. Native exceptions propagate and fault the driver, preventing retries of partially changed native state. Reset after a fault requires caller reconciliation of native state and pending time, retirement of presentation histories, and a fresh timer epoch. The driver does not install hooks or establish shared-clock ownership. Its current planner is shared with the existing analysis component.

The owned426930/41BB90 replay now uses this driver instead of immediately counting planned steps. Reproduce focused failure/reentrancy checks with `analysis/build_fixed_step_driver_probe.ps1`. Rebuild timer captures with `analysis/build_scope_unwind.ps1`, `analysis/TestFrameTiming.exe analysis/native-timing-fixture.bin --presentation-cadence-only`, and `python analysis/verify_presentation_cadence.py` after planner changes, before scene replay. Live input/UI/movie ownership, worker clock fences and fixed-step driver installation remain unfinished.

The shared-clock inventory (`analysis/audit_shared_clocks.py`) byte-checks2,016 fully paginated IDA references to ten scalar clock cells and accounts for every selected absolute-address literal in the supported executable. This covers354 gameplay-interval references across220 recognized functions and1,574 cumulative-game-time references across436 recognized functions. Functionless references and indirect/computed aliases remain unresolved for ownership; inventory completeness does not prove a safe override.

`analysis/audit_clock_worker_ownership.py` binds the installed ScumLibLEC_Retail.dll SHA-256 (`dae04cfe87ec3434c23bb97d1c7b04f560f5f4e9f11fd31f9408fa0f1c8ee3b3`) and five scheduler/guard exports. The main-update entry's LECMultiprocUpdateSupressing call only decrements a counter; it does not join workers. Movie callbacks4111B0/419080 are submitted through LECMultiprocCall and read10D4008 before invoking the rendering path that acquires RenderWhileLoadingCriticalSection. That renderer lock alone cannot fence these clock readers. An installed driver still needs explicit worker lifetime and transition/reconciliation handling; neither audit executes game or library entry points.

`ClockDomainGate.h` is the clock admission primitive for the pending live driver. Foreign callbacks must retain a lease before native submission, including queued callbacks; admitted callbacks propagate their lease to descendants. A writer closes unrelated admission and waits for that entire reader subtree before modifying clocks. The owner then retains simulation children, closes new owner submissions when draining, and waits for transitive descendants before restoring clocks and releasing the scope. Native callbacks run without the gate lock. Timeout cannot release an active simulation subtree. Same-thread attempts to acquire/drain their own reader lease fail instead of waiting on themselves. Epochs reject stale scopes after native-state reconciliation.

The owned threaded test connects this gate to FixedStepDriver: completed-step accounting stays unchanged while a simulation grandchild is held, commits after drain, and remains uncommitted after a child exception. The exception drains its lease; clock restoration permits baseline readers while both gate and driver require explicit native-state reconciliation before another simulation scope. Reproduce with `analysis/build_clock_domain_gate_probe.ps1`. The gate is now linked into TFUPoseHistory.dll through explicit APIs; the launcher does not install it. Complete callback routing, submission-envelope lifetime/failure handling, all reader coverage and live worker ownership remain unfinished. Gate storage must outlive every retained callback, and denied admission forbids unprotected fallback execution.

`ClockJobRouter.h/.cpp` retains LEC-style callback envelopes for both the submit call and exactly one callback. Inline completion, callback completion before the submit call returns, queued descendants and close are handled separately. Thread-local callback context propagates foreign/simulation job leases through descendants; owner bindings require the gate's current scope. Denied admission leaves the result untouched and does not call the native submitter; a future raw adapter must defer or report that failure rather than fabricate a synchronous handle0. Unknown submission exceptions keep the callback reference and make uncertainty sticky until explicit native scheduler quiescence. Root exceptions restore thread-local context and mark the clock scope failed; pending simulation callbacks still forbid early clock restoration.

`analysis/build_clock_job_router_probe.ps1` compiles the router and independently verifies21 observations. Its inline adapter executes the complete407-byte original LECMultiprocCall body on the disabled-pool path, with source-validated operands relocated to owned zeroed global cells and unexpected callees trapped. Queue/early-completion scheduling remains an owned adapter, including an uncertain submit exception followed by a late callback. Source callback state, arguments, labels and native return values are preserved in the checked paths. Neither PE entry point executes. The router is now linked into TFUPoseHistory.dll through the ABI1.20 explicit APIs; it is not installed by the game launcher; asynchronous native scheduler integration and complete caller/reader routing remain unfinished. Stop callers and join native jobs before unloading callback machine code; metadata counters alone do not prove callback epilogues returned.

`analysis/build_native_clock_scheduler_probe.ps1` now executes seven original LEC routines together in owned relocated storage:407-byte submit,316-byte worker, native semaphore decrement/try-decrement/increment, finished query and wait. Two owned worker slots, real Windows semaphores and a dedicated owned static-TLS template preserve native field offsets. The native submitter publishes callback/context/argument/label/group fields, returns/reuses native job IDs, and executes its profiled caller-thread fallback when both workers are held. The original worker clears job fields and advances its ID before returning its idle semaphore. A native parent may finish while its retained descendant still uses the simulation clock; the gate waits for that descendant before restoration.

Nine independently checked pool snapshots and complete profile blocks verify these paths through ClockJobRouter, including disabled/enabled native group propagation, blocked completion waiting, close with worker descendants and joined workers before code/TLS release. Tick sampling is an observed deterministic adapter. Native initialization is never called; worker termination uses an owned wait-adapter ExitThread after completed work, so no native shutdown contract is established. No game/library PE entry point executes. Actual game scheduler routing, all clock-reader coverage, initialization/shutdown ownership and live installation remain unfinished.

ABI1.20 (`0x00010014`,67 exports) links ClockDomainGate and ClockJobRouter into TFUPoseHistory.dll. Explicit APIs configure the immutable original submitter, submit retained jobs, acquire/invoke/drain/release clock scopes, close roots, inspect status, and reconcile a quiescent backend with a fresh monotonically allocated epoch. They neither patch IAT entries nor write native clock cells. Rejected admission preserves the result and must not be interpreted as synchronous native handle0. The caller must restore native clocks only after successful drain and must stop callers/join actual native jobs before shutdown/unload.

Shutdown rejects active clock APIs, scopes, callback envelopes or uncertainty without closing admission. This avoids interrupting a callback that still needs to retain descendants. Native callback/root faults preserve exception propagation and drain API/TLS bookkeeping; failed scopes quarantine later simulation admission. Explicit reconciliation requires closed roots, no outstanding work, native scheduler/state/clock/history reconciliation, and no competing module invocation. Reconciliation and module restart allocate distinct epochs even when scope IDs repeat; old tokens reject before native callback access. Native partial state is not rolled back by this API.

The native scheduler probe now runs both directly and through these DLL exports, loading the DLL after its two owned original-worker threads already exist. Both modes independently match the same nine pool/clock/profile observations. Thirteen DLL lifecycle and three fault/status records additionally verify busy/reentrant shutdown rejection, admission preservation, exception accounting, quarantine, repeated reconciliation and restart. Rebuild using `analysis/build_native_clock_scheduler_probe.ps1` after building the DLL. The game launcher still does not load this backend; complete frame/clock/counter ownership, actual import routing, installation and live physics/judder verification remain unfinished.
