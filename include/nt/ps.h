/*
 * MinNT - ps.h
 * Process structure: EPROCESS/ETHREAD skeletons with the embedded
 * KPROCESS/KTHREAD split, exactly as NT layers Ke under Ps.
 */

#ifndef _PS_H_
#define _PS_H_

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/ob.h>

/* ---- Dispatcher-visible parts (Ke layer) -------------------------------- */

typedef struct _KPROCESS {
    ULONG64  DirectoryTableBase;        /* CR3 for this address space      */
    LIST_ENTRY ThreadListHead;          /* KTHREADs of this process        */
    KPRIORITY BasePriority;
    ULONG    ActiveThreads;
} KPROCESS, *PKPROCESS;

typedef enum _KTHREAD_STATE {
    Initialized = 0,
    Ready,
    Running,
    Standby,
    Terminated,
    Waiting,
} KTHREAD_STATE;

typedef struct _KTHREAD {
    struct _KPROCESS *Process;
    PVOID    KernelStack;               /* stack top                       */
    ULONG_PTR StackPointer;             /* saved RSP at context switch     */
    KTHREAD_STATE State;
    KPRIORITY Priority;
    LIST_ENTRY ThreadListEntry;         /* -> KPROCESS.ThreadListHead      */
    LIST_ENTRY WaitListEntry;           /* -> dispatcher ready/wait queue  */
    ULONG_PTR WaitStatus;               /* status returned from wait       */
    struct _DISPATCHER_HEADER *WaitObject; /* object we're waiting on      */
    ULONG    Quantum;                    /* ticks until preemption         */
} KTHREAD, *PKTHREAD;

/* ---- Executive parts (Ps layer) ------------------------------------------ */

typedef struct _VAD_INFO {
    ULONG64 WorkingSetSize;             /* bytes in working set            */
    ULONG64 CommitCharge;               /* pages committed                 */
    ULONG64 VirtualSize;                /* total virtual address space     */
} VAD_INFO, *PVAD_INFO;

typedef struct _EPROCESS {
    KPROCESS Pcb;                       /* MUST be first, like real NT     */
    HANDLE   UniqueProcessId;
    LIST_ENTRY ActiveProcessLinks;      /* -> PsActiveProcessHead          */
    CHAR     ImageFileName[16];         /* the classic 15+NUL              */
    LIST_ENTRY ThreadListHead;          /* ETHREADs                        */
    /* Per-process CPU time accumulated since process start. */
    ULONG    KernelTime;                /* 100ns units spent in kernel     */
    ULONG    UserTime;                  /* 100ns units spent in user       */
    /* Memory accounting (filled in by the MM). */
    VAD_INFO Vm;
    /* Handle / thread counts (filled by Ob/Ps). */
    ULONG    HandleCount;
    ULONG    ThreadCount;
    /* Wine compatibility: per-process Win32 subsystem hints. */
    ULONG    WineFlags;
    ULONG    WineVersion;               /* e.g. 0x600 = Vista, 0xA00 = 10 */
    ULONG    WineDesktopW;
    ULONG    WineDesktopH;
    ULONG    WineGraphicsMode;          /* 0=GDI, 1=OpenGL, 2=Vulkan */
    ULONG    WineAudioMode;             /* 0=ALSA, 1=Pulse, 2=OSS, 3=MinNT */
    UCHAR    WineDllOverrides[64][128]; /* up to 64 module -> native/builtin */
    ULONG    WineDllOverrideCount;
} EPROCESS, *PEPROCESS;

typedef struct _ETHREAD {
    KTHREAD Tcb;                        /* MUST be first                   */
    HANDLE  UniqueThreadId;
    PEPROCESS ThreadProcess;
    LIST_ENTRY ThreadListEntry;         /* -> EPROCESS.ThreadListHead      */
    VOID (NTAPI *StartRoutine)(PVOID);
    PVOID StartContext;
    ULONG   UserThreadState;            /* USER subsystem thread state flags */
} ETHREAD, *PETHREAD;

/* ---- Globals -------------------------------------------------------------- */

extern LIST_ENTRY  PsActiveProcessHead;
extern PEPROCESS   PsInitialSystemProcess;   /* "System", PID 4            */
extern POBJECT_TYPE PsProcessType;
extern POBJECT_TYPE PsThreadType;

/* ---- API ------------------------------------------------------------------ */

NTSTATUS NTAPI PsInitSystem(VOID);

NTSTATUS NTAPI PsCreateSystemProcess(const CHAR *ImageName, PEPROCESS *OutProcess);

NTSTATUS NTAPI PsCreateSystemThread(PEPROCESS Process,
                                    VOID (NTAPI *StartRoutine)(PVOID),
                                    PVOID StartContext,
                                    PETHREAD *OutThread);

NTSTATUS NTAPI PsCreateUserThread(PEPROCESS Process,
                                   PVOID UserEntryPoint,
                                   PVOID UserStackBase,
                                   ULONG64 UserStackSize,
                                   PETHREAD *OutThread);

#define PsGetCurrentProcess() \
    ((PEPROCESS)KeGetCurrentThread()->Process)

PKTHREAD NTAPI KeGetCurrentThread(VOID);
HANDLE NTAPI PsGetCurrentProcessId(VOID);
PETHREAD NTAPI PsGetCurrentThread(VOID);

#define PsGetCurrentThreadId() \
    (((PETHREAD)KeGetCurrentThread())->UniqueThreadId)

/* ---- Scheduler (round-robin over ready queue) ----------------------------- */

VOID NTAPI KiInitializeScheduler(VOID);
VOID NTAPI KiReadyThread(PKTHREAD Thread);
VOID NTAPI KiDispatchNextThread(VOID);       /* voluntary yield             */

#endif /* _PS_H_ */
