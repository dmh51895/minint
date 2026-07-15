/*
 * MinNT - init/kiinit_minimal.c
 * Tiny stub for setupldr.
 *
 * setupldr is a separate binary that doesn't go through the full
 * KiSystemStartup. We provide just enough symbols to satisfy the
 * linker when pulling in hal/rtl/ex/fs/setupapi modules.
 */

#define DECLSPEC_NORETURN __attribute__((noreturn))
#include <nt/ke.h>
#include <nt/dispatcher.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ps.h>
#include <nt/ob.h>

PVOID KiMultibootInfo;

/* KeInitializeSpinLock stub for setupldr. setupldr doesn't pull in
 * ke/irql.c (which has scheduler dependencies) but fs/fat32.c needs
 * this. */
VOID NTAPI KeInitializeSpinLock(PKSPIN_LOCK SpinLock)
{
    if (SpinLock) *SpinLock = 0;
}

KIRQL NTAPI KeAcquireSpinLockRaiseToDpc(PKSPIN_LOCK SpinLock)
{
    if (SpinLock) *SpinLock = 1;
    return 0;
}

VOID NTAPI KeReleaseSpinLock(PKSPIN_LOCK SpinLock, KIRQL NewIrql)
{
    (void)SpinLock; (void)NewIrql;
}

/* Stubs that the linker may complain about but aren't called. */
NTSTATUS NTAPI PsCreateSystemProcess(const CHAR *ImageName, PEPROCESS *OutProcess)
{
    (void)ImageName; (void)OutProcess;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI PsCreateSystemThread(PEPROCESS Process, VOID (NTAPI *StartRoutine)(PVOID),
                                    PVOID StartContext, PETHREAD *OutThread)
{
    (void)Process; (void)StartRoutine; (void)StartContext; (void)OutThread;
    return STATUS_NOT_SUPPORTED;
}

DECLSPEC_NORETURN VOID NTAPI KeBugCheckEx(ULONG Code, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4)
{
    (void)Code; (void)P1; (void)P2; (void)P3; (void)P4;
    for (;;) { __asm__ __volatile__("hlt"); }
}

PETHREAD NTAPI PsGetCurrentThread(VOID) { return NULL; }

/* The compiler may complain about variable arguments macros. Provide
 * a no-op placeholder for KeStallExecutionProcessor. */
VOID NTAPI KeStallExecutionProcessor(ULONG Microseconds)
{
    (void)Microseconds;
    /* Burn cycles with a PAUSE hint. */
    __asm__ __volatile__("0:\n\tpause\n\tdec %%ecx\n\tjnz 0b" : : "c"(Microseconds) : "cc");
}

/* Stub AcpiReboot for setupldr. The setupldr reboots by writing to
 * the ACPI reset register, or as a fallback, the keyboard
 * controller's reset line. */
NTSTATUS NTAPI AcpiReboot(VOID)
{
    /* Pulse the keyboard controller reset line. */
    UCHAR tmp = 0;
    __asm__ __volatile__("inb $0x64, %0" : "=a"(tmp));
    (void)tmp;
    __asm__ __volatile__("outb %0, $0x64" : : "a"((UCHAR)0xFE));
    return STATUS_SUCCESS;
}

/* Settings stubs for setupldr. The settings infrastructure lives in
 * win32k/settings.c in the full kernel; setupldr doesn't need it
 * because the install just writes files and reboots. */
NTSTATUS NTAPI SettingsSetComputerName(PCWSTR Name) { (void)Name; return STATUS_SUCCESS; }
NTSTATUS NTAPI SettingsSetRegisteredOwner(PCWSTR Name) { (void)Name; return STATUS_SUCCESS; }
NTSTATUS NTAPI SettingsSetThemeName(PCWSTR Name) { (void)Name; return STATUS_SUCCESS; }
NTSTATUS NTAPI SettingsSetAccentColor(PCWSTR Color) { (void)Color; return STATUS_SUCCESS; }
NTSTATUS NTAPI SettingsSetTelemetryEnabled(BOOLEAN Enabled) { (void)Enabled; return STATUS_SUCCESS; }
NTSTATUS NTAPI SettingsSetErrorReporting(BOOLEAN Enabled) { (void)Enabled; return STATUS_SUCCESS; }
NTSTATUS NTAPI SettingsSetActivityHistory(BOOLEAN Enabled) { (void)Enabled; return STATUS_SUCCESS; }
NTSTATUS NTAPI SettingsSetDiagnosticData(BOOLEAN Enabled) { (void)Enabled; return STATUS_SUCCESS; }
NTSTATUS NTAPI SettingsSetTpmRequired(BOOLEAN Enabled) { (void)Enabled; return STATUS_SUCCESS; }
NTSTATUS NTAPI SettingsSetRequireOnlineAccount(BOOLEAN Enabled) { (void)Enabled; return STATUS_SUCCESS; }

/* Kernel image accessors. In setupldr we don't have a "kernel image"
 * loaded in the same sense as the full kernel - the install copies
 * the full minint.elf from disk, not from memory. */
PVOID NTAPI MmGetKernelImageBase(VOID)
{
    return (PVOID)0x100000;  /* default load address */
}

/* MmMapIoSpace stub for setupldr. In setupldr we don't have a full
 * memory manager; just use identity-mapping for low MMIO addresses. */
PVOID NTAPI MmMapIoSpace(ULONG64 PhysAddr, ULONG64 Size, ULONG Cache)
{
    (void)Size; (void)Cache;
    return (PVOID)(ULONG_PTR)PhysAddr;
}

ULONG64 NTAPI MmMapPage(ULONG64 PhysAddr, PVOID VirtAddr)
{
    (void)PhysAddr; (void)VirtAddr;
    return PhysAddr;
}

VOID NTAPI MmFreePhysicalPage(ULONG64 PhysAddr)
{
    (void)PhysAddr;
}

ULONG64 NTAPI MmAllocatePhysicalPage(ULONG Pte)
{
    (void)Pte;
    return 0x200000;  /* arbitrary low memory region */
}

/* boot/profile.c calls into boot/registry.c. setupldr doesn't use
 * the full subsystem registry so stub these. */
VOID NTAPI BootMarkSubsystemInitialized(ULONG Id)
{
    (void)Id;
}

VOID NTAPI KeConnectInterrupt(ULONG Vector, PKINTERRUPT_ROUTINE Routine)
{
    (void)Vector; (void)Routine;
}

ULONG LapicMode = 0;
PVOID LapicBase = NULL;

PVOID NTAPI CmGetRootKey(VOID) { return NULL; }
NTSTATUS NTAPI CmQueryValue(PVOID Key, PCWSTR Value, PVOID Buffer, ULONG BufferLen, PULONG OutLen)
{ (void)Key; (void)Value; (void)Buffer; (void)BufferLen; (void)OutLen; return STATUS_SUCCESS; }

LONG NTAPI KeSetEvent(PKEVENT Event, KPRIORITY Increment, BOOLEAN Wait)
{ (void)Event; (void)Increment; (void)Wait; return 0; }

NTSTATUS NTAPI CmInitSystem(VOID) { return STATUS_SUCCESS; }

LIST_ENTRY PsActiveProcessHead = { &PsActiveProcessHead, &PsActiveProcessHead };

VOID NTAPI KeInitializeEvent(PKEVENT Event, ULONG Type, BOOLEAN State)
{ (void)Event; (void)Type; (void)State; }

NTSTATUS NTAPI ObCreateObject(POBJECT_TYPE Type, SIZE_T BodySize, PCUNICODE_STRING Name, PVOID *OutObject)
{ (void)Type; (void)BodySize; (void)Name; (void)OutObject; return STATUS_SUCCESS; }

NTSTATUS NTAPI KeWaitForSingleObject(PVOID Object, KWAIT_REASON Reason, KPROCESSOR_MODE Mode,
                                     BOOLEAN Alertable, PLARGE_INTEGER Timeout)
{
    (void)Object; (void)Reason; (void)Mode; (void)Alertable; (void)Timeout;
    return STATUS_SUCCESS;
}

PKTHREAD NTAPI KeGetCurrentThread(VOID) { return NULL; }

/* ObCloseHandle stub. */
NTSTATUS NTAPI ObCloseHandle(HANDLE Handle) { (void)Handle; return STATUS_SUCCESS; }
NTSTATUS NTAPI ObInsertHandle(PVOID Body, PHANDLE OutHandle) { (void)Body; (void)OutHandle; return STATUS_SUCCESS; }
VOID NTAPI ObDereferenceObject(PVOID Body) { (void)Body; }
NTSTATUS NTAPI ObReferenceObjectByHandle(HANDLE Handle, POBJECT_TYPE ExpectedType, PVOID *OutObject)
{ (void)Handle; (void)ExpectedType; (void)OutObject; return STATUS_SUCCESS; }
ULONG NTAPI MmGetKernelImageSize(VOID)
{
    return 0;
}

/* Performance counter stub for setupldr. */
VOID NTAPI KeQueryPerformanceCounter(PLARGE_INTEGER Counter, PLARGE_INTEGER Frequency)
{
    (void)Counter; (void)Frequency;
}
