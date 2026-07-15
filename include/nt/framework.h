/*
 * MinNT - include/nt/framework.h
 * Headers for shell namespace, display topology, ETW, print spooler,
 * reliability, touch, IME, gamepad, MMC, COM, boot config.
 */

#ifndef _FRAMEWORK_H_
#define _FRAMEWORK_H_

#include <nt/ntdef.h>

/* ---- Type definitions used by framework modules ---- */

#ifndef NSITEMID_DEFINED
typedef ULONG NSITEMID;
#define NSITEMID_DEFINED
#endif

#ifndef MMCNODEID_DEFINED
typedef ULONG MMCNODEID;
#define MMCNODEID_DEFINED
#endif

/* Touch contact */
typedef struct _TOUCH_CONTACT {
    ULONG ContactId;
    ULONG X;
    ULONG Y;
    ULONG Pressure;
    BOOLEAN Active;
    BOOLEAN InUse;
} TOUCH_CONTACT, *PTOUCH_CONTACT;

/* ---- Shell namespace ---- */
NTSTATUS NTAPI NsInit(VOID);
NSITEMID NTAPI NsRegisterItem(NSITEMID Parent, const WCHAR *DisplayName, BOOLEAN IsFolder);
NTSTATUS NTAPI NsUnregisterItem(NSITEMID Id);
NTSTATUS NTAPI NsGetFullPath(NSITEMID Id, PWCHAR Buffer, ULONG BufferLen);
ULONG    NTAPI NsEnumChildren(NSITEMID Parent, ULONG MaxCount, NSITEMID *pIds, PWCHAR *pNames, PBOOLEAN pIsFolder);
NTSTATUS NTAPI NsGetDisplayName(NSITEMID Id, PWCHAR Buffer, ULONG BufferLen);
NSITEMID NTAPI NsGetRootId(VOID);

/* ---- Display topology ---- */
NTSTATUS NTAPI DisplayTopologyInit(VOID);
ULONG    NTAPI DisplayEnum(ULONG MaxCount, ULONG *pIds, ULONG *pWidths, ULONG *pHeights, ULONG *pFlags);
NTSTATUS NTAPI DisplayGetInfo(ULONG Id, ULONG *pWidth, ULONG *pHeight, ULONG *pRefresh, LONG *pX, LONG *pY, ULONG *pOrientation, PULONG pFlags);
NTSTATUS NTAPI DisplaySetInfo(ULONG Id, ULONG Width, ULONG Height, ULONG RefreshRate, LONG PosX, LONG PosY, ULONG Orientation);
NTSTATUS NTAPI DisplaySetMode(ULONG Mode);
ULONG    NTAPI DisplayGetMode(VOID);
NTSTATUS NTAPI DisplaySetPrimary(ULONG Id);

/* ---- ETW ---- */
NTSTATUS NTAPI EtwInit(VOID);
NTSTATUS NTAPI EtwRegisterProvider(GUID *ProviderId, const CHAR *Name);
NTSTATUS NTAPI EtwEnableProvider(GUID *ProviderId, ULONG EnableMask);
NTSTATUS NTAPI EtwDisableProvider(GUID *ProviderId);
NTSTATUS NTAPI EtwStartSession(const CHAR *Name, GUID *SessionGuid, ULONG Flags, ULONG BufferSize);
NTSTATUS NTAPI EtwStopSession(ULONG SessionId);
NTSTATUS NTAPI EtwWrite(GUID *ProviderId, GUID *EventId, ULONG EventLevel, PVOID Payload, ULONG PayloadSize);
ULONG    NTAPI EtwReadSession(ULONG SessionId, PVOID OutBuffer, ULONG MaxBytes);
ULONG    NTAPI EtwEnumProviders(ULONG MaxCount, PCHAR *pNames, GUID *pIds);
ULONG    NTAPI EtwEnumSessions(ULONG MaxCount, PCHAR *pNames, PULONG pFlags);
ULONG64  NTAPI EtwGetEventCount(VOID);

/* ---- Print spooler ---- */
NTSTATUS NTAPI SpoolerInit(VOID);
ULONG    NTAPI SpoolAddPrinter(const WCHAR *Name, BOOLEAN IsDefault);
NTSTATUS NTAPI SpoolRemovePrinter(ULONG PrinterId);
ULONG    NTAPI SpoolEnumPrinters(ULONG MaxCount, ULONG *pIds, PCHAR *pNames, PBOOLEAN pIsDefault);
ULONG    NTAPI SpoolGetDefaultPrinter(VOID);
ULONG    NTAPI SpoolSubmitJob(ULONG PrinterId, const WCHAR *DocumentName);
NTSTATUS NTAPI SpoolWriteJob(ULONG JobId, PVOID Data, ULONG DataSize);
NTSTATUS NTAPI SpoolCancelJob(ULONG JobId);
ULONG    NTAPI SpoolEnumJobs(ULONG MaxCount, ULONG *pJobIds, ULONG *pStatus, ULONG *pBytes);
NTSTATUS NTAPI SpoolGetJobStatus(ULONG JobId, PULONG pStatus);

/* ---- Boot config ---- */
NTSTATUS NTAPI BootCfgInit(VOID);
NTSTATUS NTAPI BootCfgAddEntry(const WCHAR *FriendlyName, const WCHAR *OsLoaderPath, ULONG TimeoutSecs);
NTSTATUS NTAPI BootCfgRemoveEntry(ULONG Id);
NTSTATUS NTAPI BootCfgSetDefault(ULONG Id);
NTSTATUS NTAPI BootCfgSetTimeout(ULONG TimeoutSecs);
ULONG    NTAPI BootCfgGetDefault(VOID);
ULONG    NTAPI BootCfgGetTimeout(VOID);
ULONG    NTAPI BootCfgEnumEntries(ULONG MaxCount, ULONG *pIds, PCHAR *pNames, PBOOLEAN pIsDefault, PULONG pTimeouts);
NTSTATUS NTAPI BootCfgGetEntry(ULONG Id, ULONG *pTimeout, PBOOLEAN pIsDefault);

/* ---- Safe mode ---- */
NTSTATUS NTAPI SafeModeInit(VOID);
BOOLEAN NTAPI SafeModeIsActive(VOID);
NTSTATUS NTAPI SafeModeSet(BOOLEAN Enable);
BOOLEAN NTAPI SafeModeBootFlagsDetected(VOID);
NTSTATUS NTAPI SafeModeGetAllowedDrivers(ULONG MaxCount, PCHAR *pNames);

/* ---- Boot menu ---- */
VOID NTAPI BootMenuInit(VOID);
VOID NTAPI BootMenuShutdown(VOID);
BOOLEAN NTAPI BootMenuIsActive(VOID);
VOID NTAPI BootMenuRender(VOID);
ULONG NTAPI BootMenuGetSelection(VOID);
NTSTATUS NTAPI BootMenuSetSelection(ULONG Selection);
NTSTATUS NTAPI BootMenuHandleKey(UCHAR Key);
NTSTATUS NTAPI BootMenuGetActionName(PCHAR Buffer, ULONG BufferLen);

/* ---- Reliability ---- */
NTSTATUS NTAPI ReliabilityInit(VOID);
NTSTATUS NTAPI ReliabilityRecordCrash(VOID);
NTSTATUS NTAPI ReliabilityRecordAppFailure(VOID);
NTSTATUS NTAPI ReliabilityRecordDriverFailure(VOID);
NTSTATUS NTAPI ReliabilityRecordDiskError(VOID);
NTSTATUS NTAPI ReliabilityRecordServiceFailure(VOID);
ULONG    NTAPI ReliabilityGetIndex(VOID);
ULONG    NTAPI ReliabilityEnumDays(ULONG MaxCount, PULONG64 pDates, PULONG pCrashes, PULONG pAppFailures, PULONG pDriverFailures, PULONG pDiskErrors, PULONG pServiceFailures);

/* ---- Touch ---- */
NTSTATUS NTAPI TouchInputInit(VOID);
NTSTATUS NTAPI TouchInputProcessEvent(ULONG ContactId, ULONG RawX, ULONG RawY, ULONG Pressure, BOOLEAN Touching);
NTSTATUS NTAPI TouchSetCallback(PVOID Callback);
NTSTATUS NTAPI TouchSetCalibration(LONG *Matrix9);
ULONG    NTAPI TouchGetContacts(PTOUCH_CONTACT OutContacts, ULONG MaxCount);
NTSTATUS NTAPI TouchSetEnabled(BOOLEAN Enable);
BOOLEAN NTAPI TouchIsEnabled(VOID);

/* ---- IME ---- */
NTSTATUS NTAPI ImeInit(VOID);
NTSTATUS NTAPI ImeRegister(ULONG Language, const WCHAR *Name);
NTSTATUS NTAPI ImeActivate(ULONG Index);
NTSTATUS NTAPI ImeProcessKey(WCHAR Key);
NTSTATUS NTAPI ImeCommit(PWCHAR OutBuffer, ULONG BufferLen);
NTSTATUS NTAPI ImeGetComposition(PWCHAR OutBuffer, ULONG BufferLen, PULONG pCompositionLen, PULONG pCaretPos);
ULONG    NTAPI ImeGetCandidates(ULONG MaxCount, PCHAR *pCandidates, PULONG pSelected);
NTSTATUS NTAPI ImeSelectCandidate(LONG Delta);
ULONG    NTAPI ImeEnum(ULONG MaxCount, PCHAR *pNames, PULONG pLanguages);
ULONG    NTAPI ImeGetState(VOID);

/* ---- Gamepad ---- */
typedef struct _GAMEPAD_STATE {
    USHORT Buttons;
    SHORT  LeftStickX;
    SHORT  LeftStickY;
    SHORT  RightStickX;
    SHORT  RightStickY;
    UCHAR  LeftTrigger;
    UCHAR  RightTrigger;
    /* Motion data (gyro + accelerometer). */
    SHORT  GyroX, GyroY, GyroZ;
    SHORT  AccelX, AccelY, AccelZ;
    /* Touchpad state. */
    USHORT TouchpadActive;
    SHORT  TouchpadX, TouchpadY;
} GAMEPAD_STATE, *PGAMEPAD_STATE;

typedef struct _GAMEPAD_CALIBRATION {
    SHORT Deadzone;
    SHORT LeftStickXMin, LeftStickXMax;
    SHORT LeftStickYMin, LeftStickYMax;
    SHORT RightStickXMin, RightStickXMax;
    SHORT RightStickYMin, RightStickYMax;
    BOOLEAN Valid;
} GAMEPAD_CALIBRATION, *PGAMEPAD_CALIBRATION;

typedef struct _GAMEPAD_RUMBLE {
    USHORT LeftMotor;     /* low-frequency, 0-65535 */
    USHORT RightMotor;    /* high-frequency, 0-65535 */
    UCHAR  LeftTrigger;   /* trigger rumble 0-255 (PS5) */
    UCHAR  RightTrigger;  /* trigger rumble 0-255 (PS5) */
} GAMEPAD_RUMBLE, *PGAMEPAD_RUMBLE;

typedef enum _GAMEPAD_TYPE {
    GamepadTypeUnknown = 0,
    GamepadTypeXbox360,
    GamepadTypeXboxOne,
    GamepadTypePS4,
    GamepadTypePS5,
    GamepadTypeSwitchPro,
    GamepadTypeSteamController,
    GamepadTypeGenericHID,
} GAMEPAD_TYPE;

NTSTATUS NTAPI GamepadInit(VOID);
ULONG    NTAPI GamepadConnect(const WCHAR *Name);
NTSTATUS NTAPI GamepadDisconnect(ULONG Id);
NTSTATUS NTAPI GamepadUpdateState(ULONG Id, PGAMEPAD_STATE NewState);
NTSTATUS NTAPI GamepadGetState(ULONG Id, PGAMEPAD_STATE OutState);
NTSTATUS NTAPI GamepadSetCalibration(ULONG Id, PGAMEPAD_CALIBRATION Calibration);
NTSTATUS NTAPI GamepadGetCalibration(ULONG Id, PGAMEPAD_CALIBRATION OutCalibration);
BOOLEAN  NTAPI GamepadIsButtonDown(ULONG Id, USHORT ButtonMask);
ULONG    NTAPI GamepadEnum(ULONG MaxCount, ULONG *pIds, PCHAR *pNames, PBOOLEAN pConnected);
NTSTATUS NTAPI GamepadSetRumble(ULONG Id, PGAMEPAD_RUMBLE Rumble);
NTSTATUS NTAPI GamepadGetRumble(ULONG Id, PGAMEPAD_RUMBLE OutRumble);
NTSTATUS NTAPI GamepadStopRumble(ULONG Id);
NTSTATUS NTAPI GamepadSetType(ULONG Id, GAMEPAD_TYPE Type);
NTSTATUS NTAPI GamepadGetType(ULONG Id, PULONG OutType);
NTSTATUS NTAPI GamepadDetectType(ULONG Id, USHORT Vid, USHORT Pid, PULONG OutType);
NTSTATUS NTAPI GamepadRegisterHotplugCallback(PVOID Callback);
NTSTATUS NTAPI GamepadNotifyHotplug(ULONG Id, BOOLEAN Connected);

/* ---- Steam Input-style action layer ---- */
NTSTATUS NTAPI SteamInputInit(VOID);
NTSTATUS NTAPI SteamInputRegisterActionSet(const CHAR *Name, ULONG Reserved);
NTSTATUS NTAPI SteamInputRegisterDigitalAction(const CHAR *Name, ULONG Reserved);
NTSTATUS NTAPI SteamInputRegisterAnalogAction(const CHAR *Name, ULONG Reserved);
ULONG64   NTAPI SteamInputConnect(ULONG Type, const CHAR *Name, USHORT Vid, USHORT Pid);
NTSTATUS  NTAPI SteamInputDisconnect(ULONG64 Handle);
NTSTATUS  NTAPI SteamInputActivateActionSet(ULONG64 Handle, ULONG ActionSetIndex);
NTSTATUS  NTAPI SteamInputRunFrame(VOID);
NTSTATUS  NTAPI SteamInputGetDigitalActionData(ULONG64 Handle, ULONG ActionIndex, PULONG OutState, PBOOLEAN OutActive);
NTSTATUS  NTAPI SteamInputGetAnalogActionData(ULONG64 Handle, ULONG ActionIndex, PSHORT OutX, PSHORT OutY, PSHORT OutZ);
ULONG     NTAPI SteamInputGetConnectedControllers(PULONG64 Handles, ULONG MaxCount);
NTSTATUS  NTAPI SteamInputGetInputTypeForHandle(ULONG64 Handle, PULONG OutType);
NTSTATUS  NTAPI SteamInputGetStringForOrigin(ULONG Origin, PCHAR OutString, ULONG MaxLen);

/* ---- Controller Remapping ---- */
NTSTATUS NTAPI RemapInit(VOID);
NTSTATUS NTAPI RemapBind(ULONG Type, USHORT Button, const CHAR *Action);
NTSTATUS NTAPI RemapUnbind(ULONG Type, USHORT Button);
NTSTATUS NTAPI RemapResolve(ULONG Type, USHORT Button, PCHAR OutAction, ULONG MaxLen);
NTSTATUS NTAPI RemapApply(ULONG GamepadId, USHORT Buttons, PCHAR OutActions, ULONG MaxLen, PULONG OutCount);
ULONG    NTAPI RemapCount(ULONG Type);

/* ---- Touchpad (Steam Controller style) ---- */
NTSTATUS NTAPI TouchpadInit(VOID);
NTSTATUS NTAPI TouchpadCreate(ULONG Width, ULONG Height, PULONG OutId);
NTSTATUS NTAPI TouchpadSetMode(ULONG Id, ULONG Mode);
NTSTATUS NTAPI TouchpadGetMode(ULONG Id, PULONG OutMode);
NTSTATUS NTAPI TouchpadSubmitContact(ULONG Id, ULONG ContactId, SHORT X, SHORT Y, UCHAR Pressure, BOOLEAN Active);
NTSTATUS NTAPI TouchpadGetAxis(ULONG Id, PSHORT OutX, PSHORT OutY);
NTSTATUS NTAPI TouchpadGetButton(ULONG Id, PBOOLEAN OutDown);
ULONG    NTAPI TouchpadEnum(PULONG OutArray, ULONG MaxCount);

/* ---- Controller Naming (OEMName/OEMData) ----------------------------- */
NTSTATUS NTAPI ControllerSetName(USHORT Vid, USHORT Pid, const CHAR *FriendlyName);
NTSTATUS NTAPI ControllerGetName(USHORT Vid, USHORT Pid, PCHAR OutName, ULONG MaxLen);
NTSTATUS NTAPI ControllerSetOemData(USHORT Vid, USHORT Pid, PUCHAR Data, ULONG Length);
NTSTATUS NTAPI ControllerGetOemData(USHORT Vid, USHORT Pid, PUCHAR OutData, ULONG MaxLen, PULONG OutLength);
NTSTATUS NTAPI ControllerRemoveEntry(USHORT Vid, USHORT Pid);
ULONG    NTAPI ControllerEnumerate(PULONG Vids, PULONG Pids, PCHAR Names, ULONG MaxCount);

/* ---- Lookaside Lists ---- */
typedef VOID (NTAPI *PWORKER_THREAD_ROUTINE)(PVOID Context);

typedef struct _NPAGED_LOOKASIDE_ENTRY {
    struct _NPAGED_LOOKASIDE_ENTRY *Next;
} NPAGED_LOOKASIDE_ENTRY, *PNPAGED_LOOKASIDE_ENTRY;

typedef struct _NPAGED_LOOKASIDE_LIST {
    CHAR Name[64];
    ULONG Tag;
    ULONG Depth;
    ULONG MaximumDepth;
    ULONG AllocateHits;
    ULONG AllocateMisses;
    ULONG FreeHits;
    ULONG FreeMisses;
    ULONG_PTR Lock;
    NPAGED_LOOKASIDE_ENTRY ListHead;
    BOOLEAN InUse;
} NPAGED_LOOKASIDE_LIST, *PNPAGED_LOOKASIDE_LIST;

/* ---- Push Locks ---- */
typedef struct _PUSH_LOCK {
    ULONG State;
    KEVENT WakeEvent;
} PUSH_LOCK, *PPUSH_LOCK;

/* ---- Executive Resources ---- */
struct _PETHREAD;
typedef struct _ETHREAD *PETHREAD;

typedef struct _ERESOURCE_THREAD_ENTRY {
    PETHREAD Thread;
    LONG SharedCount;
    BOOLEAN ExclusiveOwner;
    struct _ERESOURCE_THREAD_ENTRY *Next;
} ERESOURCE_THREAD_ENTRY, *PERESOURCE_THREAD_ENTRY;

typedef struct _ERESOURCE {
    ULONG State;
    ULONG_PTR Lock;
    KSEMAPHORE SharedWaiters;
    KSEMAPHORE ExclusiveWaiter;
    PETHREAD ExclusiveOwner;
    PERESOURCE_THREAD_ENTRY Owners;
    BOOLEAN InUse;
    CHAR Name[64];
} ERESOURCE, *PERESOURCE;

/* ---- Rundown Protection ---- */
typedef struct _RUNDOWN_REF {
    ULONG Count;
    BOOLEAN RundownCompleted;
    BOOLEAN EventInitialized;
    KEVENT RundownEvent;
} RUNDOWN_REF, *PRUNDOWN_REF;

/* ---- Worker Threads ---- */
typedef struct _WORK_ITEM {
    PWORKER_THREAD_ROUTINE Routine;
    PVOID Context;
    ULONG Flags;
    ULONG DelayMs;
    LARGE_INTEGER DueTime;
    BOOLEAN InUse;
} WORK_ITEM, *PWORK_ITEM;

typedef enum _WORK_QUEUE_TYPE {
    CriticalWorkItem = 0,
    DelayedWorkItem = 1,
    HyperCriticalWorkItem = 2,
} WORK_QUEUE_TYPE;

/* ---- Lookaside Lists ---- */
NTSTATUS NTAPI ExInitializeNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside, ULONG Tag);
NTSTATUS NTAPI ExDeleteNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside);
PVOID    NTAPI ExAllocateFromNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside);
VOID     NTAPI ExFreeToNPagedLookasideList(PNPAGED_LOOKASIDE_LIST Lookaside, PVOID Entry);
NTSTATUS NTAPI ExRegisterLookaside(const CHAR *Name, ULONG Tag, PNPAGED_LOOKASIDE_LIST *OutList);
ULONG    NTAPI ExLookasideGetDepth(PNPAGED_LOOKASIDE_LIST Lookaside);
ULONG    NTAPI ExLookasideGetAllocateHits(PNPAGED_LOOKASIDE_LIST Lookaside);

/* ---- Push Locks ---- */
NTSTATUS NTAPI ExInitializePushLock(PPUSH_LOCK Lock);
VOID     NTAPI ExAcquirePushLockShared(PPUSH_LOCK Lock);
VOID     NTAPI ExReleasePushLockShared(PPUSH_LOCK Lock);
VOID     NTAPI ExAcquirePushLockExclusive(PPUSH_LOCK Lock);
VOID     NTAPI ExReleasePushLockExclusive(PPUSH_LOCK Lock);
VOID     NTAPI ExAcquirePushLockExclusiveRecursive(PPUSH_LOCK Lock, ULONG Count);
ULONG    NTAPI ExGetPushLockState(PPUSH_LOCK Lock);

/* ---- Executive Resources ---- */
NTSTATUS NTAPI ExInitializeResourceLite(ERESOURCE *Resource, const CHAR *Name);
NTSTATUS NTAPI ExDeleteResourceLite(ERESOURCE *Resource);
NTSTATUS NTAPI ExAcquireResourceSharedLite(ERESOURCE *Resource, BOOLEAN Wait);
NTSTATUS NTAPI ExReleaseResourceLite(ERESOURCE *Resource);
NTSTATUS NTAPI ExAcquireResourceExclusiveLite(ERESOURCE *Resource, BOOLEAN Wait);
ULONG    NTAPI ExGetResourceState(ERESOURCE *Resource);

/* ---- Rundown Protection ---- */
NTSTATUS NTAPI ExInitializeRundownProtection(PRUNDOWN_REF Ref);
VOID     NTAPI ExAcquireRundownProtection(PRUNDOWN_REF Ref);
VOID     NTAPI ExReleaseRundownProtection(PRUNDOWN_REF Ref);
VOID     NTAPI ExWaitForRundownProtectionRelease(PRUNDOWN_REF Ref);
NTSTATUS NTAPI ExWaitForRundownProtectionReleaseAsync(PRUNDOWN_REF Ref, PKEVENT Event);
VOID     NTAPI ExRundownCompleted(PRUNDOWN_REF Ref);
BOOLEAN  NTAPI ExAcquireRundownProtectionEx(PRUNDOWN_REF Ref, ULONG Count);
ULONG    NTAPI ExGetRundownProtectionCount(PRUNDOWN_REF Ref);

/* ---- Worker Threads ---- */
NTSTATUS NTAPI ExInitializeWorkerFactory(VOID);
NTSTATUS NTAPI ExQueueWorkItem(PWORK_ITEM Item, ULONG QueueType);
NTSTATUS NTAPI ExInitializeWorkItem(PWORK_ITEM Item, PWORKER_THREAD_ROUTINE Routine, PVOID Context);
ULONG    NTAPI ExGetWorkerCount(VOID);
ULONG    NTAPI ExGetQueueDepth(VOID);

/* ---- Audio Engine ---- */
typedef enum _AUDIO_DIRECTION {
    AudioDirectionRender = 0,
    AudioDirectionCapture = 1,
} AUDIO_DIRECTION;

typedef enum _AUDIO_FORMAT_TAG {
    AudioFormatPcm = 0,
    AudioFormatFloat = 1,
    AudioFormatAC3 = 2,
} AUDIO_FORMAT_TAG;

typedef struct _AUDIO_FORMAT {
    AUDIO_FORMAT_TAG Tag;
    ULONG SampleRate;
    USHORT Channels;
    USHORT BitsPerSample;
    ULONG ByteRate;
    USHORT BlockAlign;
} AUDIO_FORMAT;

NTSTATUS NTAPI AudioEngineInit(VOID);
NTSTATUS NTAPI AudioEngineShutdown(VOID);
NTSTATUS NTAPI AudioRegisterEndpoint(const CHAR *Name, AUDIO_DIRECTION Direction,
                                      AUDIO_FORMAT *Format, PULONG OutId);
NTSTATUS NTAPI AudioSetEndpointVolume(ULONG EndpointId, ULONG Volume);
NTSTATUS NTAPI AudioGetEndpointVolume(ULONG EndpointId, PULONG OutVolume);
NTSTATUS NTAPI AudioSetEndpointMute(ULONG EndpointId, BOOLEAN Muted);
NTSTATUS NTAPI AudioEnumEndpoints(PULONG OutIds, ULONG MaxCount);
NTSTATUS NTAPI AudioGetEndpointInfo(ULONG EndpointId, PCHAR OutName, ULONG MaxLen,
                                     PULONG OutDirection, PULONG OutSampleRate);
NTSTATUS NTAPI AudioCreateSession(const CHAR *Name, ULONG EndpointId, ULONG ProcessId,
                                    AUDIO_FORMAT *Format, PULONG OutSessionId);
NTSTATUS NTAPI AudioDestroySession(ULONG SessionId);
NTSTATUS NTAPI AudioSetSessionVolume(ULONG SessionId, ULONG Volume);
NTSTATUS NTAPI AudioSetSessionMute(ULONG SessionId, BOOLEAN Muted);
NTSTATUS NTAPI AudioWriteSession(ULONG SessionId, PVOID Buffer, ULONG Frames);
NTSTATUS NTAPI AudioEnumSessions(ULONG EndpointId, PULONG OutIds, ULONG MaxCount);
ULONG    NTAPI AudioGetEndpointCount(VOID);
ULONG    NTAPI AudioGetSessionCount(VOID);

/* Forward declarations for cross-module types. */
struct _EPROCESS;
typedef struct _EPROCESS *PEPROCESS;
struct _KSEMAPHORE;
typedef struct _KSEMAPHORE KSEMAPHORE, *PKSEMAPHORE;

/* ---- APCs ---- */
NTSTATUS NTAPI KeInitializeApc(PKAPC Apc, PETHREAD Thread, PVOID KernelRoutine,
                                PVOID RundownRoutine, PVOID NormalRoutine,
                                ULONG ProcessorMode, PVOID NormalContext);
NTSTATUS NTAPI KeInsertQueueApc(PKAPC Apc, PVOID SystemArgument1,
                                PVOID SystemArgument2, UCHAR PriorityBoost);
BOOLEAN  NTAPI KeRemoveQueueApc(PKAPC Apc);
NTSTATUS NTAPI KeDeliverApc(PETHREAD Thread, ULONG Reserved);
NTSTATUS NTAPI PsQueueUserApc(PETHREAD Thread, PVOID ApcRoutine, PVOID NormalContext,
                              PVOID SystemArgument1, PVOID SystemArgument2);

/* ---- Job Objects ---- */
NTSTATUS NTAPI PsCreateJob(const CHAR *Name, PULONG OutJobId);
NTSTATUS NTAPI PsDeleteJob(ULONG JobId);
NTSTATUS NTAPI PsAssignProcessToJob(ULONG JobId, PEPROCESS Process);
NTSTATUS NTAPI PsRemoveProcessFromJob(ULONG JobId, PEPROCESS Process);
NTSTATUS NTAPI PsSetJobLimits(ULONG JobId, ULONG64 PerProcessUserTime,
                              ULONG64 PerProcessKernelTime, ULONG64 TotalUser,
                              ULONG64 TotalKernel, ULONG64 WorkingSet, ULONG ProcessCount);
NTSTATUS NTAPI PsQueryJobInfo(ULONG JobId, PULONG OutActive, PULONG OutTerminated,
                              PULONG64 OutUserTime, PULONG64 OutKernelTime);
ULONG    NTAPI PsGetJobCount(VOID);

/* ---- Fibers ---- */
NTSTATUS NTAPI PsCreateFiber(PVOID StackBase, ULONG StackSize,
                              PVOID StartRoutine, PVOID StartParameter,
                              PULONG OutFiberId);
NTSTATUS NTAPI PsDeleteFiber(ULONG FiberId);
NTSTATUS NTAPI PsSwitchToFiber(ULONG FiberId);
ULONG    NTAPI PsGetCurrentFiberId(VOID);
ULONG    NTAPI PsConvertThreadToFiber(VOID);
NTSTATUS NTAPI PsConvertFiberToThread(ULONG FiberId);

/* ---- Firewall ---- */
typedef enum _FW_ACTION { FwActionAllow = 0, FwActionDrop = 1, FwActionLog = 2 } FW_ACTION;
typedef enum _FW_DIRECTION { FwDirInbound = 0, FwDirOutbound = 1, FwDirBoth = 2 } FW_DIRECTION;
NTSTATUS NTAPI FwInit(VOID);
NTSTATUS NTAPI FwAddRule(FW_ACTION Action, FW_DIRECTION Direction, UCHAR Protocol,
                          ULONG SourceIp, ULONG SourceMask, USHORT SourcePort,
                          ULONG DestIp, ULONG DestMask, USHORT DestPort,
                          const CHAR *Name);
NTSTATUS NTAPI FwRemoveRule(ULONG RuleId);
NTSTATUS NTAPI FwEnableRule(ULONG RuleId, BOOLEAN Enabled);
FW_ACTION NTAPI FwEvaluatePacket(UCHAR Protocol, ULONG SourceIp, USHORT SourcePort,
                                    ULONG DestIp, USHORT DestPort, FW_DIRECTION Direction);
NTSTATUS NTAPI FwSetEnabled(BOOLEAN Enabled);
BOOLEAN  NTAPI FwIsEnabled(VOID);
ULONG    NTAPI FwEnumRules(PULONG OutIds, ULONG MaxCount);
NTSTATUS NTAPI FwGetRule(ULONG RuleId, PULONG OutAction, PULONG OutDirection,
                          PULONG OutProtocol, PULONG OutSourceIp, PULONG OutDestIp);

/* ---- DHCP ---- */
NTSTATUS NTAPI DhcpInit(VOID);
NTSTATUS NTAPI DhcpStart(ULONG AdapterIndex, const UCHAR *Mac);
NTSTATUS NTAPI DhcpStop(ULONG AdapterIndex);
NTSTATUS NTAPI DhcpRenew(ULONG AdapterIndex);
NTSTATUS NTAPI DhcpRelease(ULONG AdapterIndex);
NTSTATUS NTAPI DhcpGetLease(ULONG AdapterIndex, PULONG OutIp, PULONG OutMask,
                             PULONG OutGateway, PULONG OutDns);
BOOLEAN  NTAPI DhcpIsBound(ULONG AdapterIndex);
ULONG    NTAPI DhcpGetLeaseCount(VOID);

/* ---- DNS ---- */
NTSTATUS NTAPI DnsInit(VOID);
NTSTATUS NTAPI DnsShutdown(VOID);
NTSTATUS NTAPI DnsRegister(const CHAR *Name, ULONG Ip, ULONG Ttl);
NTSTATUS NTAPI DnsRemove(const CHAR *Name);
NTSTATUS NTAPI DnsResolve(const CHAR *Name, PULONG OutIp);
ULONG    NTAPI DnsCacheSize(VOID);
NTSTATUS NTAPI DnsFlushCache(VOID);

/* ---- WinSxS / Fusion ---- */
NTSTATUS NTAPI FusionInit(VOID);
NTSTATUS NTAPI FusionRegisterAssembly(const CHAR *Name, const CHAR *Version,
                                       const CHAR *Culture, const UCHAR *PublicKeyToken,
                                       ULONG TokenLength, const CHAR *Path,
                                       const CHAR *Manifest);
NTSTATUS NTAPI FusionResolve(const CHAR *Name, const CHAR *Version,
                              CHAR *OutPath, ULONG MaxLen);
NTSTATUS NTAPI FusionUnregisterAssembly(const CHAR *Name);
ULONG    NTAPI FusionEnumAssemblies(PCHAR Names, ULONG MaxCount);

/* ---- COM Apartments ---- */
NTSTATUS NTAPI ComInit(VOID);
NTSTATUS NTAPI CoInitialize(PVOID Reserved, ULONG InitFlags);
VOID     NTAPI CoUninitialize(VOID);
ULONG    NTAPI CoGetApartmentId(VOID);
NTSTATUS NTAPI CoAptRegisterClassObject(ULONG64 Clsid, PETHREAD ServerThread,
                                       PULONG OutCookie);
NTSTATUS NTAPI CoAptRevokeClassObject(ULONG Cookie);
ULONG    NTAPI CoAptGetClassObjectCount(VOID);
ULONG    NTAPI CoGetApartmentCount(VOID);

/* ---- exFAT ---- */
NTSTATUS NTAPI ExFatInit(VOID);
NTSTATUS NTAPI ExFatMount(const CHAR *DevicePath);
NTSTATUS NTAPI ExFatCreateEntry(const CHAR *Path, ULONG64 Size);
ULONG    NTAPI ExFatGetEntryCount(VOID);

/* ---- ISO 9660 ---- */
NTSTATUS NTAPI Iso9660Init(VOID);
NTSTATUS NTAPI Iso9660Mount(const CHAR *DevicePath);
NTSTATUS NTAPI Iso9660Open(const CHAR *Path, PULONG OutSector, PULONG OutSize);
ULONG    NTAPI Iso9660GetEntryCount(VOID);

/* ---- Filter Manager ---- */
struct _FLT_FILTER;
typedef struct _FLT_FILTER *PFLT_FILTER;
typedef enum _FLT_OPERATION {
    FltOpPreCreate = 0, FltOpPostCreate, FltOpPreRead, FltOpPostRead,
    FltOpPreWrite, FltOpPostWrite, FltOpPreCleanup, FltOpPostCleanup,
    FltOpMax,
} FLT_OPERATION;
NTSTATUS NTAPI FltMgrInit(VOID);
NTSTATUS NTAPI FltRegisterFilter(const CHAR *Name, ULONG Altitude, PFLT_FILTER *OutFilter);
NTSTATUS NTAPI FltAttachFilter(PFLT_FILTER Filter);
NTSTATUS NTAPI FltDetachFilter(PFLT_FILTER Filter);
NTSTATUS NTAPI FltUnregisterFilter(PFLT_FILTER Filter);
NTSTATUS NTAPI FltSetCallback(PFLT_FILTER Filter, FLT_OPERATION Operation,
                               PVOID Callback, PVOID Context);
NTSTATUS NTAPI FltInvokeCallbacks(FLT_OPERATION Operation, PVOID Request);
ULONG    NTAPI FltGetAttachedCount(VOID);

/* ---- Wine Compatibility Layer ---- */
NTSTATUS NTAPI WineInit(VOID);
NTSTATUS NTAPI WineShutdown(VOID);
NTSTATUS NTAPI WineSetVersion(ULONG Version);
NTSTATUS NTAPI WineGetVersion(PULONG OutVersion);
NTSTATUS NTAPI WineGetVersionName(PCHAR OutBuffer, ULONG MaxLen);
NTSTATUS NTAPI WineSetDllOverride(const CHAR *DllName, ULONG Mode, const CHAR *NativePath);
NTSTATUS NTAPI WineRemoveDllOverride(const CHAR *DllName);
NTSTATUS NTAPI WineGetDllOverride(const CHAR *DllName, PULONG OutMode, PCHAR OutNativePath, ULONG MaxLen);
ULONG    NTAPI WineEnumDllOverrides(PCHAR DllNames, ULONG MaxCount);
NTSTATUS NTAPI WineSetDriveMapping(CHAR Letter, const CHAR *Target, const CHAR *Type);
NTSTATUS NTAPI WineRemoveDriveMapping(CHAR Letter);
NTSTATUS NTAPI WineGetDriveMapping(CHAR Letter, PCHAR OutTarget, ULONG MaxLen, PCHAR OutType, ULONG MaxType);
NTSTATUS NTAPI WineSetVirtualDesktop(BOOLEAN Enabled, ULONG Width, ULONG Height);
NTSTATUS NTAPI WineGetVirtualDesktop(PBOOLEAN OutEnabled, PULONG OutW, PULONG OutH);
NTSTATUS NTAPI WineSetGraphicsMode(ULONG Mode);
NTSTATUS NTAPI WineGetGraphicsMode(PULONG OutMode);
NTSTATUS NTAPI WineSetAudioMode(ULONG Mode);
NTSTATUS NTAPI WineGetAudioMode(PULONG OutMode);
NTSTATUS NTAPI WineSetEsync(BOOLEAN Enabled);
NTSTATUS NTAPI WineSetFsync(BOOLEAN Enabled);
NTSTATUS NTAPI WineSetHideWineVersion(BOOLEAN Enabled);
NTSTATUS NTAPI WineSetEnableLogging(BOOLEAN Enabled);
NTSTATUS NTAPI WineSetPrefixPath(const CHAR *Path);
NTSTATUS NTAPI WineGetPrefixPath(PCHAR OutPath, ULONG MaxLen);
NTSTATUS NTAPI WineRunExecutable(const CHAR *Path, const CHAR *Args);
NTSTATUS NTAPI WineLoadConfig(void);
NTSTATUS NTAPI WineSaveConfig(void);
typedef struct _WINE_RUN_HISTORY {
    CHAR Path[260];
    CHAR Args[256];
    LARGE_INTEGER Timestamp;
    ULONG ExitCode;
    BOOLEAN InUse;
} WINE_RUN_HISTORY, *PWINE_RUN_HISTORY;
ULONG    NTAPI WineGetRunHistory(PWINE_RUN_HISTORY OutBuffer, ULONG MaxCount);
NTSTATUS NTAPI WineDetectBinaryFormat(const CHAR *Path, PULONG OutFormat);
NTSTATUS NTAPI WineShouldRoute(const CHAR *Path, PBOOLEAN OutShouldRoute);

/* ---- Task Manager ---- */
NTSTATUS NTAPI TaskmgrInit(VOID);
NTSTATUS NTAPI TaskmgrRefresh(VOID);
NTSTATUS NTAPI TaskmgrSetActiveTab(ULONG Tab);
ULONG    NTAPI TaskmgrGetProcessCount(VOID);
NTSTATUS NTAPI TaskmgrGetProcess(ULONG Index, PCHAR OutName, ULONG MaxLen,
                                 PULONG OutPid, PULONG OutCpu, PULONG OutMem);
ULONG    NTAPI TaskmgrGetWindowCount(VOID);
NTSTATUS NTAPI TaskmgrGetWindow(ULONG Index, PCHAR OutTitle, ULONG MaxLen,
                                PULONG OutPid);
NTSTATUS NTAPI TaskmgrEndProcess(ULONG Pid);
NTSTATUS NTAPI TaskmgrSetPriority(ULONG Pid, ULONG PriorityClass);
ULONG    NTAPI TaskmgrGetCpuUsage(VOID);
ULONG    NTAPI TaskmgrGetMemoryUsage(VOID);
NTSTATUS NTAPI TaskmgrGetHistoryEntry(ULONG Index, PULONG OutCpu, PULONG OutMem, PULONG OutStamp);

/* ---- File Properties with Compatibility Tab ---- */
NTSTATUS NTAPI PropertiesInit(VOID);
NTSTATUS NTAPI PropsOpen(const CHAR *Path);
NTSTATUS NTAPI PropsSetUseWine(const CHAR *Path, BOOLEAN Enabled);
NTSTATUS NTAPI PropsGetUseWine(const CHAR *Path, PBOOLEAN OutEnabled);
NTSTATUS NTAPI PropsSetWindowsVersion(const CHAR *Path, ULONG Version);
NTSTATUS NTAPI PropsGetWindowsVersion(const CHAR *Path, PULONG OutVersion);
NTSTATUS NTAPI PropsSetVirtualDesktop(const CHAR *Path, BOOLEAN Enabled, ULONG Width, ULONG Height);
NTSTATUS NTAPI PropsGetVirtualDesktop(const CHAR *Path, PBOOLEAN OutEnabled, PULONG OutW, PULONG OutH);
NTSTATUS NTAPI PropsSetDllOverride(const CHAR *Path, const CHAR *DllName, ULONG Mode, const CHAR *NativePath);
NTSTATUS NTAPI PropsRemoveDllOverride(const CHAR *Path, const CHAR *DllName);
ULONG    NTAPI PropsEnumDllOverrides(const CHAR *Path, PCHAR Names, ULONG MaxCount);
NTSTATUS NTAPI PropsApplyForExecutable(const CHAR *Path);
NTSTATUS NTAPI PropsLaunch(const CHAR *Path, const CHAR *Args);

/* ---- Wine prefix initialization (wineboot equivalent) ---- */
NTSTATUS NTAPI WinebootInitModule(VOID);
NTSTATUS NTAPI WinebootInit(const CHAR *PrefixPath);
NTSTATUS NTAPI WinebootInitDefault(VOID);
NTSTATUS NTAPI WinebootShutdown(VOID);

/* ---- OS Installer ---- */
NTSTATUS NTAPI OsInstallInit(VOID);
ULONG    NTAPI OsInstallScanDisks(VOID);
ULONG    NTAPI OsInstallScanPartitions(ULONG DiskNumber);
NTSTATUS NTAPI OsInstallGetDisk(ULONG Index, PULONG OutNumber, PULONG64 OutSize,
                                PCHAR OutModel, ULONG MaxLen);
NTSTATUS NTAPI OsInstallGetPartition(ULONG Index, PULONG OutNumber, PULONG64 OutSize,
                                     PCHAR OutFs, ULONG MaxLen, PBOOLEAN OutBootable);
NTSTATUS NTAPI OsInstallSelectDisk(ULONG DiskIndex);
NTSTATUS NTAPI OsInstallSelectPartition(ULONG PartitionIndex);
NTSTATUS NTAPI OsInstallSetFormat(BOOLEAN Format);
NTSTATUS NTAPI OsInstallSetComputerName(const CHAR *Name);
NTSTATUS NTAPI OsInstallSetUserName(const CHAR *Name);
NTSTATUS NTAPI OsInstallRun(PCHAR ProgressMessage, PULONG Percent);
ULONG    NTAPI OsInstallGetPhase(VOID);
ULONG    NTAPI OsInstallGetDiskCount(VOID);
ULONG    NTAPI OsInstallGetPartitionCount(VOID);
BOOLEAN  NTAPI OsInstallIsComplete(VOID);
BOOLEAN  NTAPI OsInstallHasFailed(VOID);
NTSTATUS NTAPI OsInstallRunTUI(VOID);
BOOLEAN  NTAPI OsInstallIsComplete(VOID);
#define OSINSTALL_MAX_PARTITIONS 16

/* ---- Boot Arguments ---- */
NTSTATUS NTAPI BootArgsParse(PVOID Mb2Info);
const CHAR *NTAPI BootArgsGetCmdLine(VOID);
BOOLEAN  NTAPI BootArgsHas(const CHAR *Flag);
BOOLEAN  NTAPI BootArgsIsInstall(VOID);
BOOLEAN  NTAPI BootArgsIsSafeMode(VOID);
BOOLEAN  NTAPI BootArgsIsTerminal(VOID);
BOOLEAN  NTAPI BootArgsIsRecovery(VOID);
BOOLEAN  NTAPI BootArgsIsDebug(VOID);
BOOLEAN  NTAPI BootArgsIsNetwork(VOID);

/* ---- Boot Profile System ---- */
typedef enum _BOOT_PROFILE {
    BootProfileNormal = 0,
    BootProfileLive,
    BootProfileInstall,
    BootProfileSafe,
    BootProfileRecovery,
    BootProfileTerminal,
    BootProfileDebug,
} BOOT_PROFILE;

NTSTATUS NTAPI BootProfileInit(VOID);
NTSTATUS NTAPI BootProfileResolve(VOID);
NTSTATUS NTAPI BootRegisterSubsystem(const CHAR *Name, NTSTATUS (*Init)(VOID));
NTSTATUS NTAPI BootMarkSubsystemInitialized(const CHAR *Name);
NTSTATUS NTAPI BootRegisterSubsystemEx(const CHAR *Name,
                                       NTSTATUS (*Init)(VOID),
                                       const CHAR **Deps, ULONG DepCount,
                                       ULONG Phase);
NTSTATUS NTAPI BootRegistryInit(VOID);
NTSTATUS NTAPI BootProfileAddRequired(BOOT_PROFILE Id, const CHAR *Name);
NTSTATUS NTAPI BootProfileAddExcluded(BOOT_PROFILE Id, const CHAR *Name);
NTSTATUS NTAPI BootProfileActivate(BOOT_PROFILE Id);
BOOT_PROFILE NTAPI BootProfileGetActive(VOID);
BOOLEAN  NTAPI BootProfileAllowsSubsystem(const CHAR *Name);
BOOLEAN  NTAPI BootProfileIsSubsystemRequired(const CHAR *Name);
NTSTATUS NTAPI BootRunSubsystem(const CHAR *Name, NTSTATUS (*Init)(VOID));
ULONG    NTAPI BootEnumSubsystems(PCHAR Names, ULONG MaxCount);
BOOLEAN  NTAPI BootIsSubsystemInitialized(const CHAR *Name);

/* ---- MMC ---- */
NTSTATUS NTAPI MmcInit(VOID);
MMCNODEID NTAPI MmcRegisterSnapIn(MMCNODEID Parent, GUID *Clsid, const WCHAR *DisplayName, const WCHAR *Description, ULONG NodeType, BOOLEAN Extendable);
ULONG    NTAPI MmcEnumChildren(MMCNODEID Parent, ULONG MaxCount, MMCNODEID *pIds);
NTSTATUS NTAPI MmcGetDisplayName(MMCNODEID Id, PWCHAR Buffer, ULONG BufferLen);
MMCNODEID NTAPI MmcGetRootId(VOID);
BOOLEAN  NTAPI MmcHasChildren(MMCNODEID Id);
NTSTATUS NTAPI MmcGetClsid(MMCNODEID Id, GUID *OutClsid);

/* ---- ACPI ---- */
NTSTATUS NTAPI AcpiInit(VOID);
NTSTATUS NTAPI AcpiEnterSleepState(ULONG State);
NTSTATUS NTAPI AcpiReboot(VOID);
NTSTATUS NTAPI AcpiGetBatteryStatus(PULONG OutPercent, PULONG OutCharging);
NTSTATUS NTAPI AcpiGetThermal(PULONG OutCelsius);
NTSTATUS NTAPI AcpiGetCpuCount(PULONG OutCount);

/* ---- WMI ---- */
typedef enum _WMI_PROP_TYPE {
    WMI_STRING = 0, WMI_UINT32, WMI_UINT64, WMI_BOOLEAN
} WMI_PROP_TYPE;

typedef struct _WMI_PROPERTY {
    CHAR Name[128];
    WMI_PROP_TYPE Type;
    ULONG Offset;
} WMI_PROPERTY, *PWMI_PROPERTY;

typedef struct _WMI_RESULT {
    ULONG Count;
    PVOID Instances;
    ULONG InstanceSize;
} WMI_RESULT, *PWMI_RESULT;

NTSTATUS NTAPI WmiInit(VOID);
NTSTATUS NTAPI WmiRegisterClass(const CHAR *ProviderName, const CHAR *ClassName,
                                PWMI_PROPERTY Props, ULONG PropCount,
                                ULONG InstanceSize);
NTSTATUS NTAPI WmiAddInstance(const CHAR *ClassName, PVOID Data, ULONG Size);
NTSTATUS NTAPI WmiQuery(const CHAR *Query, PWMI_RESULT OutResult);
VOID     NTAPI WmiFreeResult(PWMI_RESULT Result);

/* ---- RPC ---- */
struct _RPC_UUID;
typedef struct _RPC_UUID RPC_UUID, *PRPC_UUID;

NTSTATUS NTAPI RpcInit(VOID);
NTSTATUS NTAPI RpcServerRegisterInterface(const CHAR *EndpointName, PRPC_UUID Uuid,
                                          ULONG VersionMajor, ULONG VersionMinor,
                                          PVOID ServerRoutine);
NTSTATUS NTAPI RpcBindingBind(PRPC_UUID Uuid, ULONG VersionMajor, ULONG VersionMinor,
                              PULONG OutBindingId);
NTSTATUS NTAPI RpcBindingUnbind(ULONG BindingId);
NTSTATUS NTAPI RpcCall(ULONG BindingId, ULONG ProcNum,
                       PVOID InData, ULONG InLen,
                       PVOID OutData, ULONG *OutLen,
                       ULONG *OutReturn);

/* ---- Reparse + Hard Links ---- */
NTSTATUS NTAPI ReparseInit(VOID);
NTSTATUS NTAPI ReparseRegisterHandler(ULONG Tag, PVOID Callback);
NTSTATUS NTAPI ReparseSetPoint(const CHAR *Path, ULONG Tag, PVOID Data, ULONG DataLength);
NTSTATUS NTAPI ReparseGetPoint(const CHAR *Path, PULONG OutTag, PVOID OutData, PULONG OutDataLength);
NTSTATUS NTAPI ReparseRemovePoint(const CHAR *Path);
NTSTATUS NTAPI ReparseResolve(const CHAR *Path, PCHAR OutTarget, PULONG OutLength);
NTSTATUS NTAPI HardLinkCreate(const CHAR *ExistingPath, const CHAR *NewPath);
NTSTATUS NTAPI HardLinkRemove(const CHAR *Path);
NTSTATUS NTAPI HardLinkGetCount(const CHAR *Path, PULONG OutCount);

/* ---- VSS ---- */
struct _VSS_SNAPSHOT;
typedef struct _VSS_SNAPSHOT VSS_SNAPSHOT, *PVSS_SNAPSHOT;

NTSTATUS NTAPI VssInit(VOID);
NTSTATUS NTAPI VssCreateSnapshot(const CHAR *Volume, PULONG OutSnapshotId);
NTSTATUS NTAPI VssQuerySnapshot(ULONG SnapshotId, PVSS_SNAPSHOT OutInfo,
                                PVOID OutDiffBuffer, ULONG *OutDiffLength);
NTSTATUS NTAPI VssRecordDiff(ULONG SnapshotId, ULONG FileId, ULONG Offset,
                             PVOID OldData, ULONG Length);
NTSTATUS NTAPI VssRestoreSnapshot(ULONG SnapshotId);
NTSTATUS NTAPI VssDeleteSnapshot(ULONG SnapshotId);

/* ---- PnP ---- */
NTSTATUS NTAPI PnpInit(VOID);
NTSTATUS NTAPI PnpCreateDevice(const CHAR *InstanceId, ULONG ParentId, PULONG OutDeviceId);
NTSTATUS NTAPI PnpAddCompatibleId(ULONG DeviceId, const CHAR *Id);
NTSTATUS NTAPI PnpRegisterDriver(const CHAR *Name, const CHAR *Provider, PVOID DriverEntry,
                                 const CHAR *CompatibleIds[], ULONG CompatibleCount,
                                 PUCHAR Signature, ULONG SignatureLength);
NTSTATUS NTAPI PnpStartDevice(ULONG DeviceId);
NTSTATUS NTAPI PnpStopDevice(ULONG DeviceId);
NTSTATUS NTAPI PnpEnumerateChildDevices(ULONG ParentId, PULONG OutArray, PULONG OutCount);

/* ---- Kernel Debugger (KD) ---- */
struct _KD_PACKET;
typedef struct _KD_PACKET KD_PACKET, *PKD_PACKET;

NTSTATUS NTAPI KdInit(ULONG Major, ULONG Minor, ULONG_PTR KernelBase);
NTSTATUS NTAPI KdConnect(const CHAR *Host, const CHAR *Port);
NTSTATUS NTAPI KdDisconnect(VOID);
NTSTATUS NTAPI KdReceivePacket(PKD_PACKET packet);

/* ---- Bundled apps ---- */
NTSTATUS NTAPI NotepadInit(VOID);
NTSTATUS NTAPI NotepadOpenFile(const CHAR *Path);
NTSTATUS NTAPI NotepadOpenDoc(PULONG OutIndex);
NTSTATUS NTAPI NotepadLoad(ULONG DocIndex, const CHAR *Path);
NTSTATUS NTAPI NotepadSave(ULONG DocIndex);
NTSTATUS NTAPI NotepadAppend(ULONG DocIndex, CHAR c);
NTSTATUS NTAPI NotepadBackspace(ULONG DocIndex);
NTSTATUS NTAPI NotepadCloseDoc(ULONG DocIndex);
ULONG    NTAPI NotepadGetLength(ULONG DocIndex);
NTSTATUS NTAPI NotepadGetBuffer(ULONG DocIndex, PCHAR OutBuffer, ULONG MaxLen, PULONG OutLen);
BOOLEAN  NTAPI NotepadIsModified(ULONG DocIndex);

NTSTATUS NTAPI CalculatorInit(VOID);
NTSTATUS NTAPI CalculatorOpen(VOID);
VOID     NTAPI CalcPress(ULONG Id, ULONG Key);
NTSTATUS NTAPI CalculatorGetDisplay(ULONG Id, PCHAR Buffer, ULONG MaxLen, PULONG OutLen);
BOOLEAN  NTAPI CalculatorIsScientific(ULONG Id);
BOOLEAN  NTAPI CalculatorHasError(ULONG Id);

/* ---- Bundled Terminal (cmd + PowerShell) ---- */
NTSTATUS NTAPI TermInit(VOID);
NTSTATUS NTAPI TermExecLine(const CHAR *line, PCHAR Output, ULONG MaxLen, PULONG OutUsed);
NTSTATUS NTAPI TermSetEnv(const CHAR *Name, const CHAR *Value);
NTSTATUS NTAPI TermGetEnv(const CHAR *Name, PCHAR OutValue, ULONG MaxLen);
NTSTATUS NTAPI TermSetCurrentDir(const CHAR *Path);
NTSTATUS NTAPI TermGetCurrentDir(PCHAR OutPath, ULONG MaxLen);
NTSTATUS NTAPI TermSetPowerShell(BOOLEAN Enabled);

/* ---- Safely Remove Hardware ---- */
NTSTATUS NTAPI SafeUsbInit(VOID);
NTSTATUS NTAPI SafeUsbRegister(const CHAR *DevicePath, CHAR VolumeLetter, PULONG OutDeviceId);
NTSTATUS NTAPI SafeUsbUnregister(ULONG DeviceId);
NTSTATUS NTAPI SafeUsbMarkDirty(ULONG DeviceId, ULONG BytesPending);
NTSTATUS NTAPI SafeUsbAcquire(ULONG DeviceId);
NTSTATUS NTAPI SafeUsbRelease(ULONG DeviceId);
NTSTATUS NTAPI SafeUsbEject(ULONG DeviceId);
NTSTATUS NTAPI SafeUsbEnum(PULONG OutArray, PULONG InOutCount);

/* ---- Disk Quotas ---- */
NTSTATUS NTAPI QuotaInit(VOID);
NTSTATUS NTAPI QuotaSet(CHAR Volume, const CHAR *User, ULONG64 Warning, ULONG64 HardLimit);
NTSTATUS NTAPI QuotaCharge(CHAR Volume, const CHAR *User, ULONG64 Bytes);
NTSTATUS NTAPI QuotaReturn(CHAR Volume, const CHAR *User, ULONG64 Bytes);
NTSTATUS NTAPI QuotaQuery(CHAR Volume, const CHAR *User, ULONG64 *Used, ULONG64 *Warning, ULONG64 *Hard);

/* ---- Roaming User Profiles ---- */
NTSTATUS NTAPI RoamingProfileInit(VOID);
NTSTATUS NTAPI RoamingProfileCreate(const CHAR *User, const CHAR *RemotePath);
NTSTATUS NTAPI RoamingProfileLogon(const CHAR *User);
NTSTATUS NTAPI RoamingProfileLogoff(const CHAR *User);
NTSTATUS NTAPI RoamingProfileRedirect(const CHAR *User, ULONG Folder, const CHAR *RemotePath);
NTSTATUS NTAPI RoamingProfileGetPath(const CHAR *User, ULONG Folder, PCHAR OutPath, ULONG MaxLen);

/* ---- Sync Center / Offline Files ---- */
NTSTATUS NTAPI SyncInit(VOID);
NTSTATUS NTAPI SyncAddShare(const CHAR *RemotePath, const CHAR *LocalCache, PULONG OutShareId);
NTSTATUS NTAPI SyncQueueWrite(ULONG ShareId, const CHAR *Path, PVOID Data, ULONG Length);
NTSTATUS NTAPI SyncReconcile(ULONG ShareId);
NTSTATUS NTAPI SyncSetOnline(ULONG ShareId, BOOLEAN Online);

/* ---- HID class driver ---- */
NTSTATUS NTAPI HidInit(VOID);
NTSTATUS NTAPI HidRegisterDevice(ULONG Type, const CHAR *Name, PULONG OutDeviceId);
NTSTATUS NTAPI HidSubmitReport(ULONG DeviceId, PVOID Report, ULONG Length);
NTSTATUS NTAPI HidGetUsageValue(ULONG DeviceId, USHORT UsagePage, USHORT Usage, PULONG OutValue);
NTSTATUS NTAPI HidUnregisterDevice(ULONG DeviceId);

/* ---- Text Services Framework ---- */
NTSTATUS NTAPI TsfInit(VOID);
NTSTATUS NTAPI TsfDictAdd(const CHAR *word);
NTSTATUS NTAPI TsfSetAutocorrect(const CHAR *From, const CHAR *To);
NTSTATUS NTAPI TsfCreateSession(PULONG OutSessionId);
NTSTATUS NTAPI TsfDestroySession(ULONG SessionId);
NTSTATUS NTAPI TsfSetComposition(ULONG SessionId, const CHAR *Text);
NTSTATUS NTAPI TsfApplyAutocorrect(PCHAR Text, ULONG MaxLen);
NTSTATUS NTAPI TsfSuggest(ULONG SessionId, PCHAR OutBuffer, ULONG MaxLen, PULONG OutCount);
NTSTATUS NTAPI TsfSpellCheck(const CHAR *Word, PBOOLEAN OutCorrect);

/* ---- Codec/Media Framework ---- */
NTSTATUS NTAPI MediaInit(VOID);
NTSTATUS NTAPI CodecRegister(const CHAR *Name, ULONG Type,
                             ULONG SampleRate, ULONG Channels, ULONG BitsPerSample,
                             PULONG OutCodecId);
NTSTATUS NTAPI DemuxerRegister(const CHAR *Name, const CHAR *Magic,
                               ULONG MagicLength, PULONG OutDemuxerId);
NTSTATUS NTAPI DemuxerDetect(PVOID Header, ULONG HeaderLength, PULONG OutDemuxerId);
NTSTATUS NTAPI MediaPlayerOpen(const CHAR *FilePath, PULONG OutPlayerId);
NTSTATUS NTAPI MediaPlayerPlay(ULONG PlayerId);
NTSTATUS NTAPI MediaPlayerPause(ULONG PlayerId);
NTSTATUS NTAPI MediaPlayerStop(ULONG PlayerId);
NTSTATUS NTAPI MediaPlayerClose(ULONG PlayerId);
NTSTATUS NTAPI MediaPlayerGetState(ULONG PlayerId, PULONG OutPlayed, PULONG OutTotal, PBOOLEAN OutPlaying);

/* ---- Windows Installer ---- */
NTSTATUS NTAPI InstInit(VOID);
NTSTATUS NTAPI InstRegisterPackage(const CHAR *Name, const CHAR *Version, const CHAR *Provider);
NTSTATUS NTAPI InstAddAction(const CHAR *Package, ULONG Action, const CHAR *Key,
                             PVOID Value, ULONG ValueLength);
NTSTATUS NTAPI InstInstall(const CHAR *Package);
NTSTATUS NTAPI InstUninstall(const CHAR *Package);
NTSTATUS NTAPI InstEnum(PULONG OutArray, PULONG InOutCount);

/* ---- TPM / Secure Boot ---- */
NTSTATUS NTAPI TpmInit(VOID);
NTSTATUS NTAPI TpmPcrExtend(ULONG Index, PVOID Data, ULONG Length);
NTSTATUS NTAPI TpmPcrRead(ULONG Index, PVOID OutBuffer, ULONG BufferLength, PULONG OutLength);
NTSTATUS NTAPI TpmCreateKey(const CHAR *Name, ULONG Type,
                            PVOID PublicKey, ULONG PublicKeyLength, PULONG OutKeyId);
NTSTATUS NTAPI TpmSeal(ULONG KeyId, PVOID Data, ULONG DataLength,
                       PVOID OutBlob, ULONG BlobLength, PULONG OutLength);
NTSTATUS NTAPI TpmUnseal(ULONG KeyId, PVOID Blob, ULONG BlobLength,
                         PVOID OutData, ULONG DataLength, PULONG OutLength);
NTSTATUS NTAPI TpmSecureBootVerifyStage(ULONG Stage);
NTSTATUS NTAPI TpmEnableSecureBoot(VOID);
BOOLEAN  NTAPI TpmIsSecureBootEnabled(VOID);

#endif /* _FRAMEWORK_H_ */
