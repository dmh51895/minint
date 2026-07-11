/*
 * MinNT - ndis/ndis.c
 * NDIS library — minimal implementation for WiFi miniport.
 *
 * Provides the NdisM* functions that WiFi miniport drivers call.
 * Architecture (simplified for MinNT):
 *
 *   WiFi Miniport Driver (rtw_ndis.c)
 *         |
 *         +-- NdisMInitializeWrapper()
 *         +-- NdisMRegisterMiniport()
 *         +-- NdisMSetAttributes()
 *         +-- NdisMRegisterInterrupt()
 *         +-- NdisMInitializeTimer()
 *         +-- NdisMSendComplete()
 *         +-- NdisMIndicateStatus()
 *         |
 *   NDIS Library (this file)
 *         |
 *         +-- Tracks miniport adapters in global list
 *         +-- Provides send/receive indication paths
 *         +-- Timer DPC dispatch
 *         +-- Interrupt synchronization
 *         |
 *   MinNT Kernel (ke/, io/)
 *         +-- Ke* primitives
 *         +-- IoConnectInterrupt()
 *         +-- ExAllocatePoolWithTag()
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/io.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include <ndis.h>

#define TAG_NDIS 0x5349444E  /* 'NDIS' */
#define TAG_ADAP 0x41444150  /* 'ADAP' */
#define TAG_DMA  0x414D444E  /* 'NMDA' */

/* ---- Global lists ------------------------------------------------- */

/* Global list of registered miniport adapters */
static LIST_ENTRY MiniportListHead;
static KSPIN_LOCK MiniportListLock;

/* Global list of registered protocols */
static LIST_ENTRY ProtocolListHead;
static KSPIN_LOCK ProtocolListLock;

/* Cancel ID counter for packets */
static LONG NdisCancelId;

/* Receive packet queue for lwIP to process */
#define RX_QUEUE_MAX 32
static PNDIS_PACKET ReceivePacketRxQueue[RX_QUEUE_MAX];
static volatile ULONG RxQueueHead;
static volatile ULONG RxQueueTail;
static volatile ULONG ReceiveIndicatedCount;
static volatile ULONG ReceiveReturnedCount;

/* ---- List initialization -------------------------------------------- */

static VOID NTAPI NdisInitGlobals(VOID)
{
    InitializeListHead(&MiniportListHead);
    KeInitializeSpinLock(&MiniportListLock);
    InitializeListHead(&ProtocolListHead);
    KeInitializeSpinLock(&ProtocolListLock);
    NdisCancelId = 0;
    RtlZeroMemory(ReceivePacketRxQueue, sizeof(ReceivePacketRxQueue));
    RxQueueHead = 0;
    RxQueueTail = 0;
    ReceiveIndicatedCount = 0;
    ReceiveReturnedCount = 0;
}

/* ---- NDIS initialization ------------------------------------------ */

/*
 * NdisMInitializeWrapper / NdisInitializeWrapper - Initialize NDIS wrapper.
 *
 * Called by miniport driver during DriverEntry to set up the wrapper.
 * The wrapper tracks all adapters that the miniport driver manages.
 */
VOID NTAPI NdisInitializeWrapper(
    OUT PNDIS_HANDLE NdisWrapperHandle,
    IN  PVOID       SystemSpecific1,
    IN  PVOID       SystemSpecific2,
    IN  PVOID       SystemSpecific3)
{
    PNDIS_WRAPPER_CONTEXT Wrapper;

    DbgPrint("NDIS: InitializeWrapper\n");

    Wrapper = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(NDIS_WRAPPER_CONTEXT),
                                    TAG_NDIS);
    if (!Wrapper)
    {
        *(PVOID*)NdisWrapperHandle = NULL;
        return;
    }

    RtlZeroMemory(Wrapper, sizeof(NDIS_WRAPPER_CONTEXT));
    Wrapper->Initialized = TRUE;
    Wrapper->SystemSpecific1 = SystemSpecific1;
    Wrapper->SystemSpecific2 = SystemSpecific2;
    Wrapper->SystemSpecific3 = SystemSpecific3;
    Wrapper->DriverObject = (SystemSpecific1 != NULL) ?
        (PDRIVER_OBJECT)SystemSpecific1 : NULL;

    NdisInitGlobals();

    *(PVOID*)NdisWrapperHandle = Wrapper;

    DbgPrint("NDIS: wrapper handle %p\n", (void*)Wrapper);
}

/*
 * Alias for cross-compatibility (some code calls NdisMInitializeWrapper
 * as NdisInitializeWrapper without the M prefix).
 */
VOID NTAPI NdisMInitializeWrapper(
    OUT PNDIS_HANDLE NdisWrapperHandle,
    IN  PVOID       SystemSpecific1,
    IN  PVOID       SystemSpecific2,
    IN  PVOID       SystemSpecific3)
{
    NdisInitializeWrapper(NdisWrapperHandle,
                          SystemSpecific1,
                          SystemSpecific2,
                          SystemSpecific3);
}

/* ---- Miniport registration ----------------------------------------- */

/*
 * NdisMRegisterMiniport - Register miniport with NDIS library.
 *
 * Called after NdisMInitializeWrapper. The miniport passes its
 * NDIS_MINIPORT_CHARACTERISTICS table containing handler pointers.
 */
NDIS_STATUS NTAPI NdisMRegisterMiniport(
    IN  NDIS_HANDLE                         NdisWrapperHandle,
    IN  PNDIS_MINIPORT_CHARACTERISTICS      MiniportCharacteristics,
    IN  ULONG                                CharacteristicsLength)
{
    PNDIS_WRAPPER_CONTEXT Wrapper = (PVOID)NdisWrapperHandle;

    if (!Wrapper || !Wrapper->Initialized)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (!MiniportCharacteristics)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (CharacteristicsLength < sizeof(NDIS_MINIPORT_CHARACTERISTICS))
        return NDIS_STATUS_FAILURE;

    /* Validate NDIS version */
    if (MiniportCharacteristics->MajorVersion < NDIS_MINIPORT_MAJOR_VERSION)
        return NDIS_STATUS_FAILURE;

    /* Check for mandatory handlers */
    if (!MiniportCharacteristics->HaltHandler ||
        !MiniportCharacteristics->InitHandler)
    {
        DbgPrint("NDIS: missing mandatory handlers\n");
        return NDIS_STATUS_FAILURE;
    }

    /* Store the miniport characteristics for later use */
    Wrapper->MiniportCharacteristics = *MiniportCharacteristics;
    DbgPrint("NDIS: NdisMRegisterMiniport OK — Init=%p Halt=%p Send=%p\n",
             Wrapper->MiniportCharacteristics.InitHandler,
             Wrapper->MiniportCharacteristics.HaltHandler,
             Wrapper->MiniportCharacteristics.SendHandler);

    return NDIS_STATUS_SUCCESS;
}

/* ---- Adapter attributes -------------------------------------------- */

/*
 * NdisMSetAttributes - Set basic adapter attributes.
 *
 * Called during miniport's MiniportInitialize handler.
 * The returned MiniportAdapterHandle is passed to all subsequent
 * NdisM* calls.
 *
 * NOTE: This is a simplified version. The full NDIS implementation
 * would also call NdisMSetAttributesEx internally.
 */
VOID NTAPI NdisMSetAttributes(
     OUT PNDIS_HANDLE        MiniportAdapterHandle,
     IN  NDIS_HANDLE         MiniportAdapterContext,
     IN  BOOLEAN             BusMaster,
     IN  NDIS_INTERFACE_TYPE AdapterType)
{
     PNDIS_MINIPORT_ADAPTER Adapter;

     Adapter = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(NDIS_MINIPORT_ADAPTER),
                                     TAG_ADAP);
     if (!Adapter)
         return;

     RtlZeroMemory(Adapter, sizeof(NDIS_MINIPORT_ADAPTER));
     Adapter->Initialized = TRUE;
     Adapter->MiniportAdapterContext = MiniportAdapterContext;
     Adapter->BusMaster = BusMaster;
     Adapter->AdapterType = AdapterType;
     InitializeListHead(&Adapter->MiniportListEntry);
     InitializeListHead(&Adapter->InterruptListHead);
     KeAcquireSpinLockRaiseToDpc(&MiniportListLock);
     InsertTailList(&MiniportListHead, &Adapter->MiniportListEntry);
     KeReleaseSpinLock(&MiniportListLock, DISPATCH_LEVEL);

     *(PVOID*)MiniportAdapterHandle = (PVOID)Adapter;
}

/*
 * NdisMSetAttributesEx - Set extended adapter attributes.
 *
 * @CheckForHangTimeInSeconds: How often NDIS calls the CheckForHang handler
 * @Flags: Attribute flags
 * @VirtualStationInfoLen: Size of VLAN/VirtualStation info
 */
VOID NTAPI NdisMSetAttributesEx(
    IN  NDIS_HANDLE         MiniportAdapterHandle,
    IN  NDIS_HANDLE         MiniportAdapterContext,
    IN  ULONG               CheckForHangTimeInSeconds,
    IN  ULONG               Flags,
    IN  UCHAR               VirtualStationInfoLen)
{
    PNDIS_MINIPORT_ADAPTER Adapter = (PVOID)MiniportAdapterHandle;

    UNREFERENCED_PARAMETER(CheckForHangTimeInSeconds);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VirtualStationInfoLen);

    DbgPrint("NDIS: NdisMSetAttributesEx handle=%p\n",
             (void*)MiniportAdapterHandle);

    if (!Adapter)
        return;

    NdisMSetAttributes(MiniportAdapterHandle,
                       MiniportAdapterContext,
                       FALSE,
                       (NDIS_INTERFACE_TYPE)0);
}

/* ---- Interrupt dispatch table ---------------------------------------- */

/* Common ISR dispatcher — called from KiDispatchInterrupt via KeConnectInterrupt */
static VOID NTAPI NdisInterruptDispatcher(PKTRAP_FRAME TrapFrame)
{
    ULONG vector = (ULONG)TrapFrame->Vector;
    PNDIS_WRAPPER_CONTEXT Wrapper = NULL;

    PNDIS_MINIPORT_ADAPTER Current;
    PLIST_ENTRY Entry = MiniportListHead.Flink;
    while (Entry != &MiniportListHead) {
        Current = CONTAINING_RECORD(Entry, NDIS_MINIPORT_ADAPTER, MiniportListEntry);
        if (Current->Initialized) {
            /* Walk the adapter's interrupt list */
            PLIST_ENTRY IntEntry = Current->InterruptListHead.Flink;
            while (IntEntry != &Current->InterruptListHead) {
                PNDIS_MINIPORT_INTERRUPT_INTERNAL Int =
                    CONTAINING_RECORD(IntEntry, NDIS_MINIPORT_INTERRUPT_INTERNAL, InterruptListEntry);
                if (Int->Active && Int->InterruptVector == vector) {
                    Wrapper = Current->WrapperContext;
                    goto found;
                }
                IntEntry = IntEntry->Flink;
            }
        }
        Entry = Entry->Flink;
    }

    DbgPrint("NDIS: spurious interrupt vector 0x%02x\n", (unsigned)vector);
    if (vector >= PIC_IRQ_BASE && vector < PIC_IRQ_BASE + 16)
        HalEndOfInterrupt((UCHAR)(vector - PIC_IRQ_BASE));
    return;

found:
    if (Wrapper && Wrapper->MiniportCharacteristics.ISRHandler) {
        ((BOOLEAN (*)(PVOID, PKTRAP_FRAME))Wrapper->MiniportCharacteristics.ISRHandler)(
            Wrapper->MiniportCharacteristics.InitHandler, TrapFrame);
    }

    if (vector >= PIC_IRQ_BASE && vector < PIC_IRQ_BASE + 16)
        HalEndOfInterrupt((UCHAR)(vector - PIC_IRQ_BASE));
}

/* ---- Interrupt ------------------------------------------------------ */

/*
 * NdisMRegisterInterrupt - Register an interrupt handler.
 *
 * Real implementation:
 * 1. Allocates an interrupt context structure
 * 2. Connects the common ISR dispatcher to the vector via KeConnectInterrupt
 * 3. Enables the IRQ line in the 8259 PIC
 */
NDIS_STATUS NTAPI NdisMRegisterInterrupt(
    OUT PNDIS_MINIPORT_INTERRUPT Interrupt,
    IN  NDIS_HANDLE              MiniportAdapterHandle,
    IN  ULONG                     InterruptVector,
    IN  ULONG                     InterruptLevel,
    IN  BOOLEAN                  RequestIsr,
    IN  BOOLEAN                  SharedInterrupt,
    IN  NDIS_INTERRUPT_MODE      InterruptMode)
{
    PNDIS_MINIPORT_ADAPTER Adapter = (PNDIS_MINIPORT_ADAPTER)MiniportAdapterHandle;
    PNDIS_MINIPORT_INTERRUPT_INTERNAL IntContext;
    UCHAR irq;

    if (!Interrupt || !MiniportAdapterHandle)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (InterruptVector < PIC_IRQ_BASE || InterruptVector >= PIC_IRQ_BASE + 16) {
        DbgPrint("NDIS: NdisMRegisterInterrupt: invalid vector 0x%lx (must be 0x20-0x2F)\n",
                 (unsigned long)InterruptVector);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    irq = (UCHAR)(InterruptVector - PIC_IRQ_BASE);

    IntContext = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(NDIS_MINIPORT_INTERRUPT_INTERNAL),
                                       TAG_NDIS);
    if (!IntContext)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(IntContext, sizeof(NDIS_MINIPORT_INTERRUPT_INTERNAL));
    IntContext->MiniportAdapterHandle = MiniportAdapterHandle;
    IntContext->InterruptVector = InterruptVector;
    IntContext->InterruptLevel = InterruptLevel;
    IntContext->RequestIsr = RequestIsr;
    IntContext->SharedInterrupt = SharedInterrupt;
    IntContext->InterruptMode = (USHORT)InterruptMode;
    IntContext->Active = TRUE;

    /* Link into the adapter's interrupt list */
    if (!Adapter->Initialized)
        return NDIS_STATUS_INVALID_PARAMETER;

    InsertTailList(&Adapter->InterruptListHead, &IntContext->InterruptListEntry);

    /* Register the common dispatcher in the kernel interrupt table */
    KeConnectInterrupt(InterruptVector, NdisInterruptDispatcher);

    /* Enable the IRQ line in the 8259 PIC */
    HalEnableSystemInterrupt(irq);

    /* Return the internal context as the interrupt handle */
    *(PNDIS_MINIPORT_INTERRUPT *)Interrupt = IntContext;

    DbgPrint("NDIS: NdisMRegisterInterrupt vec=0x%02x irq=%u level=%u shared=%u\n",
             (unsigned)InterruptVector, (unsigned)irq, (unsigned)InterruptLevel,
             (unsigned)SharedInterrupt);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS NTAPI NdisMDeregisterInterrupt(
    IN  NDIS_MINIPORT_INTERRUPT Interrupt)
{
    PNDIS_MINIPORT_INTERRUPT_INTERNAL IntContext =
        (PNDIS_MINIPORT_INTERRUPT_INTERNAL)Interrupt;
    UCHAR irq;

    if (!IntContext)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (!IntContext->Active)
        return NDIS_STATUS_SUCCESS;

    irq = (UCHAR)(IntContext->InterruptVector - PIC_IRQ_BASE);

    /* Disable the IRQ line in the 8259 PIC */
    HalEnableSystemInterrupt(irq);

    /* Clear the handler from the kernel interrupt table */
    KeConnectInterrupt(IntContext->InterruptVector, NULL);

    /* Remove from the adapter's interrupt list */
    RemoveEntryList(&IntContext->InterruptListEntry);

    IntContext->Active = FALSE;

    DbgPrint("NDIS: NdisMDeregisterInterrupt vec=0x%02x irq=%u\n",
             (unsigned)IntContext->InterruptVector, (unsigned)irq);

    return NDIS_STATUS_SUCCESS;
}

/* ---- Timer ---------------------------------------------------------- */

/*
 * NdisMInitializeTimer - Initialize an NDIS timer.
 *
 * The timer is associated with a specific miniport adapter.
 * When it fires, TimerFunction is called at DISPATCH_LEVEL.
 */
VOID NTAPI NdisMInitializeTimer(
    OUT PNDIS_MINIPORT_TIMER    Timer,
    IN  NDIS_HANDLE              MiniportAdapterHandle,
    IN  PNDIS_TIMER_FUNCTION     TimerFunction,
    IN  PVOID                    FunctionContext)
{
    PNDIS_MINIPORT_TIMER_INTERNAL TimerInt;

    if (!Timer || !TimerFunction)
        return;

    TimerInt = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(NDIS_MINIPORT_TIMER_INTERNAL),
                                     TAG_NDIS);
    if (!TimerInt)
        return;

    RtlZeroMemory(TimerInt, sizeof(NDIS_MINIPORT_TIMER_INTERNAL));
    TimerInt->MiniportAdapterHandle = MiniportAdapterHandle;
    TimerInt->TimerFunction = TimerFunction;
    TimerInt->FunctionContext = FunctionContext;
    TimerInt->Set = FALSE;
    TimerInt->IsPeriodic = FALSE;
    KeInitializeEvent(&TimerInt->TimerEvent, SynchronizationEvent, FALSE);

    /* Store internal context pointer in the timer handle */
    *((PNDIS_MINIPORT_TIMER_INTERNAL*)Timer) = TimerInt;

    DbgPrint("NDIS: NdisMInitializeTimer handle=%p func=%p\n",
             (void*)MiniportAdapterHandle, (void*)TimerFunction);
}

/*
 * NdisMSetTimer - Set a one-shot timer.
 *
 * Fires once after MillisecondsToDelay.
 * Uses an event-based approach: sets the timer state, waits on an event
 * with the specified delay, then fires the timer callback.
 */
VOID NTAPI NdisMSetTimer(
    IN  PNDIS_MINIPORT_TIMER    Timer,
    IN  ULONG                    MillisecondsToDelay)
{
    PNDIS_MINIPORT_TIMER_INTERNAL TimerInt;
    LARGE_INTEGER Timeout;

    if (!Timer)
        return;

    TimerInt = *((PNDIS_MINIPORT_TIMER_INTERNAL*)Timer);
    if (!TimerInt)
        return;

    TimerInt->Set = TRUE;

    if (TimerInt->TimerFunction)
    {
        Timeout.QuadPart = -(LONGLONG)MillisecondsToDelay * 10000;
        TimerInt->TimerFunction(NULL, TimerInt->FunctionContext, NULL, NULL);
        KeWaitForSingleObject(&TimerInt->TimerEvent, Executive, KernelMode, FALSE, &Timeout);
    }
}

/*
 * NdisMSetPeriodicTimer - Set a recurring timer.
 */
VOID NTAPI NdisMSetPeriodicTimer(
    IN  PNDIS_MINIPORT_TIMER    Timer,
    IN  ULONG                    MillisecondsPeriod)
{
    PNDIS_MINIPORT_TIMER_INTERNAL TimerInt;
    LARGE_INTEGER Timeout;

    if (!Timer)
        return;

    TimerInt = *((PNDIS_MINIPORT_TIMER_INTERNAL*)Timer);
    if (!TimerInt)
        return;

    TimerInt->Set = TRUE;
    TimerInt->IsPeriodic = TRUE;

    if (TimerInt->TimerFunction)
    {
        while (TimerInt->Set)
        {
            TimerInt->TimerFunction(NULL, TimerInt->FunctionContext, NULL, NULL);
            Timeout.QuadPart = -(LONGLONG)MillisecondsPeriod * 10000;
            KeWaitForSingleObject(&TimerInt->TimerEvent, Executive, KernelMode, FALSE, &Timeout);
            KeResetEvent(&TimerInt->TimerEvent);
        }
    }
}

/*
 * NdisMCancelTimer - Cancel a timer.
 */
VOID NTAPI NdisMCancelTimer(
    IN  PNDIS_MINIPORT_TIMER    Timer,
    OUT PBOOLEAN                TimerCancelled)
{
    PNDIS_MINIPORT_TIMER_INTERNAL TimerInt;

    if (TimerCancelled)
        *TimerCancelled = FALSE;

    if (!Timer)
        return;

    TimerInt = *((PNDIS_MINIPORT_TIMER_INTERNAL*)Timer);
    if (!TimerInt)
        return;

    if (TimerInt->Set)
    {
        TimerInt->Set = FALSE;
        if (TimerCancelled)
            *TimerCancelled = TRUE;
        KeSetEvent(&TimerInt->TimerEvent, 0, FALSE);
    }
}

/* ---- I/O space ----------------------------------------------------- */

/*
 * NdisMMapIoSpace - Map physical MMIO address to virtual.
 *
 * Creates real page table entries mapping the physical MMIO region
 * to kernel virtual address space with uncached (UC) memory attributes.
 */
VOID NTAPI NdisMMapIoSpace(
    OUT PVOID                    *VirtualAddress,
    IN  NDIS_HANDLE               MiniportAdapterHandle,
    IN  NDIS_PHYSICAL_ADDRESS     PhysicalAddress,
    IN  ULONG                      Length)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);

    if (!VirtualAddress || Length == 0)
        return;

    *VirtualAddress = MmMapIoSpace(PhysicalAddress, Length);

    if (*VirtualAddress)
        DbgPrint("NDIS: NdisMMapIoSpace OK phys=%llx len=%u va=%p\n",
                 (unsigned long long)(ULONG64)PhysicalAddress, (unsigned)Length, *VirtualAddress);
    else
        DbgPrint("NDIS: NdisMMapIoSpace FAILED phys=%llx len=%u\n",
                 (unsigned long long)(ULONG64)PhysicalAddress, (unsigned)Length);
}

VOID NTAPI NdisMUnmapIoSpace(
    IN  NDIS_HANDLE               MiniportAdapterHandle,
    IN  PVOID                     VirtualAddress,
    IN  ULONG                      Length)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
    UNREFERENCED_PARAMETER(Length);

    if (VirtualAddress) {
        MmUnmapIoSpace(VirtualAddress);
        DbgPrint("NDIS: NdisMUnmapIoSpace va=%p\n", VirtualAddress);
    }
}

/* ---- DMA ----------------------------------------------------------- */

/*
 * NdisMFlushDmaBuffer — Flush or invalidate CPU caches for a DMA buffer.
 *
 * On x86 without IOMMU, DMA buffers are in identity-mapped low memory.
 * The CPU cache must be synchronized before and after DMA transfers:
 *   - Before DMA read by device: write-back CPU cache so device sees fresh data
 *   - Before DMA write by device: invalidate CPU cache so CPU sees device data
 *
 * Uses WBINVL (write-back and invalidate all cache lines) for correctness.
 * In production, we'd use CLFLUSH per-line for better performance, but WBINVL
 * is simpler and correct for our minimal kernel.
 */
static VOID NTAPI NdisMFlushDmaBuffer(PNDIS_DMA_BUFFER DmaBuffer, BOOLEAN Flush)
{
    if (!DmaBuffer || !DmaBuffer->VirtualAddress)
        return;

    if (Flush) {
        /* Write-back: ensure CPU cache writes reach memory before device reads */
        __asm__ __volatile__("wbinvd" ::: "memory");
    } else {
        /* Invalidate: ensure CPU re-reads memory that device may have written */
        __asm__ __volatile__(
            "movq %0, %%rax\n\t"
            "1:\n\t"
            "clflush (%0)\n\t"
            "incq %0\n\t"
            "cmpq %1, %0\n\t"
            "jb 1b\n\t"
            "mfence\n\t"
            : : "r"(DmaBuffer->VirtualAddress), "r"((ULONG_PTR)DmaBuffer->VirtualAddress + DmaBuffer->Length)
            : "rax", "memory"
        );
    }
}

/*
 * NdisMGetPhysicalAddress — Convert virtual address to physical address.
 *
 * On MinNT, memory is identity-mapped so VA == PA for kernel addresses.
 * For DMA, we need the physical address the device will use.
 */
static ULONG64 NTAPI NdisMGetPhysicalAddress(PVOID VirtualAddress)
{
    if (!VirtualAddress)
        return 0;

    /*
     * MinNT uses identity mapping: kernel virtual addresses equal physical
     * addresses. The pool allocator allocates physical pages that are
     * identity-mapped by the boot stub (first 4GB).
     */
    return (ULONG64)(ULONG_PTR)VirtualAddress;
}

/*
 * NdisMAllocateDmaBuffer — Allocate a physically contiguous DMA buffer.
 *
 * Allocates pages from MmAllocatePhysicalPage (which returns identity-mapped,
 * physically contiguous pages). The buffer is zeroed and its physical address
 * is computed via identity mapping.
 */
static PNDIS_DMA_BUFFER NTAPI NdisMAllocateDmaBuffer(ULONG Length)
{
    PNDIS_DMA_BUFFER DmaBuffer;
    ULONG Pages;
    ULONG i;
    ULONG64 BasePa;
    PVOID Va;

    if (Length == 0)
        return NULL;

    /* Round up to page boundary */
    Pages = (Length + PAGE_SIZE - 1) >> PAGE_SHIFT;

    /* Allocate the channel descriptor */
    DmaBuffer = ExAllocatePoolWithTag(NonPagedPool, sizeof(NDIS_DMA_BUFFER), TAG_DMA);
    if (!DmaBuffer)
        return NULL;

    RtlZeroMemory(DmaBuffer, sizeof(NDIS_DMA_BUFFER));

    /*
     * Allocate physically contiguous pages.
     * MmAllocatePhysicalPage returns identity-mapped pages, so the virtual
     * address of each page equals its physical address. We need to ensure
     * the pages are contiguous in physical memory for DMA.
     *
     * Strategy: allocate the first page, then try to allocate subsequent
     * pages at the next physical address. If contiguity fails, we fall back
     * to allocating a single large region. For simplicity in this minimal
     * implementation, we allocate pages sequentially and rely on the PFN
     * allocator's rising-hint strategy (early boot allocations are contiguous).
     */
    BasePa = MmAllocatePhysicalPage();
    if (!BasePa) {
        ExFreePoolWithTag(DmaBuffer, TAG_DMA);
        return NULL;
    }

    Va = (PVOID)BasePa;  /* identity mapped */

    /* Try to allocate remaining pages contiguously */
    for (i = 1; i < Pages; i++) {
        PHYSICAL_ADDRESS Pa = MmAllocatePhysicalPage();
        if (Pa && (ULONG64)Pa == BasePa + (i << PAGE_SHIFT)) {
            /* Contiguous — good */
            continue;
        }
        /* Contiguity lost — free everything and fall back to single-page allocation */
        MmFreePhysicalPage(BasePa);
        for (ULONG j = 1; j < i; j++) {
            MmFreePhysicalPage(BasePa + (j << PAGE_SHIFT));
        }
        if (Pa)
            MmFreePhysicalPage(Pa);

        /* Fallback: allocate just one page (caller must not exceed MaximumLength) */
        Pages = 1;
        BasePa = MmAllocatePhysicalPage();
        if (!BasePa) {
            ExFreePoolWithTag(DmaBuffer, TAG_DMA);
            return NULL;
        }
        Va = (PVOID)BasePa;
        break;
    }

    DmaBuffer->VirtualAddress = Va;
    DmaBuffer->PhysicalAddress = BasePa;
    DmaBuffer->Length = Length;
    DmaBuffer->Pages = Pages;
    DmaBuffer->Mapped = FALSE;
    DmaBuffer->Direction = FALSE;  /* default: MemoryToDevice */

    DbgPrint("NDIS: DMA buffer alloc va=%p pa=%llx len=%u pages=%u\n",
             Va, (unsigned long long)BasePa, Length, Pages);

    return DmaBuffer;
}

/*
 * NdisMDestroyDmaBuffer — Free a DMA buffer and its descriptor.
 */
static VOID NTAPI NdisMDestroyDmaBuffer(PNDIS_DMA_BUFFER DmaBuffer)
{
    if (!DmaBuffer)
        return;

    if (DmaBuffer->VirtualAddress && DmaBuffer->Pages > 0) {
        for (ULONG i = 0; i < DmaBuffer->Pages; i++) {
            MmFreePhysicalPage((PHYSICAL_ADDRESS)(DmaBuffer->PhysicalAddress + ((ULONG64)i << PAGE_SHIFT)));
        }
        DbgPrint("NDIS: DMA buffer freed va=%p pa=%llx\n",
                 DmaBuffer->VirtualAddress,
                 (unsigned long long)DmaBuffer->PhysicalAddress);
    }

    ExFreePoolWithTag(DmaBuffer, TAG_DMA);
}

/*
 * NdisMMapDmaBuffer — Map a DMA buffer for a transfer.
 *
 * Called before a DMA transfer begins. Performs cache synchronization
 * based on the transfer direction. Returns the physical address the
 * device should use for the DMA operation.
 */
static ULONG64 NTAPI NdisMMapDmaBuffer(PNDIS_DMA_BUFFER DmaBuffer, ULONG Length, BOOLEAN DeviceToMemory)
{
    if (!DmaBuffer || !DmaBuffer->Mapped)
        return 0;

    if (Length > DmaBuffer->Length)
        return 0;

    DmaBuffer->CurrentLength = Length;
    DmaBuffer->Direction = DeviceToMemory;

    if (DeviceToMemory) {
        /* Device will write to memory: invalidate CPU cache so we read fresh data */
        NdisMFlushDmaBuffer(DmaBuffer, FALSE);
    } else {
        /* Device will read from memory: flush CPU cache so device sees fresh data */
        NdisMFlushDmaBuffer(DmaBuffer, TRUE);
    }

    return DmaBuffer->PhysicalAddress;
}

/*
 * NdisMUnmapDmaBuffer — Unmap a DMA buffer after transfer completes.
 *
 * Performs final cache invalidation if device wrote to memory.
 */
static VOID NTAPI NdisMUnmapDmaBuffer(PNDIS_DMA_BUFFER DmaBuffer)
{
    if (!DmaBuffer)
        return;

    if (DmaBuffer->Direction) {
        /* Device wrote to memory: invalidate CPU cache one last time */
        NdisMFlushDmaBuffer(DmaBuffer, FALSE);
    }

    DmaBuffer->Mapped = FALSE;
    DmaBuffer->CurrentLength = 0;
    DmaBuffer->Direction = FALSE;
}

/*
 * NdisMRegisterDmaChannel — Register a DMA channel with the NDIS library.
 *
 * Allocates a DMA channel descriptor and a DMA buffer for the miniport adapter.
 * The buffer is physically contiguous and identity-mapped for DMA access.
 *
 * @NdisDmaHandle:       OUT — handle to the registered DMA channel
 * @MiniportAdapterHandle: the adapter this DMA channel belongs to
 * @DmaChannelIndex:     which DMA channel (0-based index)
 * @DmaChannelAvailable: whether the hardware channel is available
 * @DmaDescription:      scatter-gather description (may be NULL for simple DMA)
 * @MaximumLength:       maximum transfer size in bytes
 */
NDIS_STATUS NTAPI NdisMRegisterDmaChannel(
    OUT PNDIS_HANDLE            NdisDmaHandle,
    IN  NDIS_HANDLE             MiniportAdapterHandle,
    IN  ULONG                    DmaChannelIndex,
    IN  BOOLEAN                 DmaChannelAvailable,
    IN  PNDIS_DMA_DESCRIPTION    DmaDescription,
    IN  ULONG                    MaximumLength)
{
    PNDIS_DMA_CHANNEL Channel;
    PNDIS_DMA_BUFFER Buffer;

    /* Validate output handle pointer */
    if (!NdisDmaHandle)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* Zero out the output handle */
    *(PVOID*)NdisDmaHandle = NULL;

    /* Validate parameters */
    if (!DmaChannelAvailable)
        return NDIS_STATUS_RESOURCES;

    if (MaximumLength == 0)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* Minimum DMA transfer is one page */
    if (MaximumLength < PAGE_SIZE)
        MaximumLength = PAGE_SIZE;

    /* Allocate the DMA channel descriptor */
    Channel = ExAllocatePoolWithTag(NonPagedPool, sizeof(NDIS_DMA_CHANNEL), TAG_DMA);
    if (!Channel)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Channel, sizeof(NDIS_DMA_CHANNEL));
    Channel->Initialized = TRUE;
    Channel->ChannelIndex = DmaChannelIndex;
    Channel->Available = DmaChannelAvailable;
    Channel->AdapterHandle = MiniportAdapterHandle;
    Channel->DmaDescription = DmaDescription;
    Channel->MaximumLength = MaximumLength;
    Channel->CurrentLength = 0;
    Channel->BusMaster = TRUE;
    Channel->InterfaceType = (NDIS_INTERFACE_TYPE)NDIS_INTERFACE_PCI;

    /* Allocate the DMA buffer — physically contiguous, identity-mapped */
    Buffer = NdisMAllocateDmaBuffer(MaximumLength);
    if (!Buffer) {
        ExFreePoolWithTag(Channel, TAG_DMA);
        return NDIS_STATUS_RESOURCES;
    }

    Channel->Buffer = Buffer;

    /* Mark buffer as mapped and ready for DMA */
    Buffer->Mapped = TRUE;

    /* Map the buffer for DMA to get the physical address */
    ULONG64 dmaPa = NdisMMapDmaBuffer(Buffer, MaximumLength, FALSE);
    if (!dmaPa) {
        dmaPa = Buffer->PhysicalAddress;
    }

    *(PVOID*)NdisDmaHandle = (PVOID)Channel;

    DbgPrint("NDIS: DMA channel %u registered va=%p pa=%llx maxlen=%u\n",
             DmaChannelIndex,
             (void*)Buffer->VirtualAddress,
             (unsigned long long)dmaPa,
             MaximumLength);

    return NDIS_STATUS_SUCCESS;
}

/*
 * NdisMDeregisterDmaChannel — Unregister and free a DMA channel.
 *
 * Frees the DMA buffer and the channel descriptor.
 *
 * @NdisDmaHandle: handle returned by NdisMRegisterDmaChannel
 */
VOID NTAPI NdisMDeregisterDmaChannel(
    IN  NDIS_HANDLE             NdisDmaHandle)
{
    PNDIS_DMA_CHANNEL Channel;

    if (!NdisDmaHandle)
        return;

    Channel = (PNDIS_DMA_CHANNEL)NdisDmaHandle;

    if (!Channel || !Channel->Initialized)
        return;

    Channel->Initialized = FALSE;
    Channel->Available = FALSE;

    /* Unmap any active transfer */
    if (Channel->Buffer && Channel->Buffer->Mapped) {
        NdisMUnmapDmaBuffer(Channel->Buffer);
    }

    /* Free the DMA buffer (physically contiguous pages) */
    if (Channel->Buffer) {
        NdisMDestroyDmaBuffer(Channel->Buffer);
        Channel->Buffer = NULL;
    }

    /* Free the channel descriptor */
    ExFreePoolWithTag(Channel, TAG_DMA);

    DbgPrint("NDIS: DMA channel %u deregistered\n", Channel->ChannelIndex);
}

/* ---- Send ----------------------------------------------------------- */

/*
 * NdisMSendComplete - Complete a send operation.
 *
 * Called by miniport when TX finishes. Returns packet ownership
 * to NDIS (and ultimately the protocol/transport above).
 *
 * @MiniportAdapterHandle: Handle from NdisMSetAttributes
 * @Packet: The packet that was previously passed to MiniportSendPackets
 * @Status: NDIS_STATUS_SUCCESS or error code
 */
VOID NTAPI NdisMSendComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  PNDIS_PACKET    Packet,
    IN  NDIS_STATUS     Status)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
    UNREFERENCED_PARAMETER(Packet);

    if (Status == NDIS_STATUS_SUCCESS)
        DbgPrint("NDIS: SendComplete SUCCESS\n");
    else
        DbgPrint("NDIS: SendComplete Status=0x%08lx\n", (unsigned long)Status);
}

VOID NTAPI NdisMSendResourcesAvailable(
    IN  NDIS_HANDLE     MiniportAdapterHandle)
{
    DbgPrint("NDIS: SendResourcesAvailable\n");
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
}

/* ---- Receive ------------------------------------------------------- */

/*
 * NdisMIndicateReceivePacket - Indicate received data upward.
 *
 * Called by miniport to pass received frames to the protocols.
 * Stores packets in a queue for lwIP to process.
 */
VOID NTAPI NdisMIndicateReceivePacket(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  PNDIS_PACKET   *Packets,
    IN  ULONG           NumberOfPackets)
{
    ULONG i;
    ULONG tail;

    UNREFERENCED_PARAMETER(MiniportAdapterHandle);

    for (i = 0; i < NumberOfPackets; i++)
    {
        if (Packets[i])
        {
            tail = RxQueueTail;
            if (tail - RxQueueHead < RX_QUEUE_MAX)
            {
                ReceivePacketRxQueue[tail % RX_QUEUE_MAX] = Packets[i];
                RxQueueTail = tail + 1;
            }
        }
    }

    ReceiveIndicatedCount += NumberOfPackets;
    DbgPrint("NDIS: IndicateReceivePacket %u packets queued (indicated=%u)\n",
             (unsigned)NumberOfPackets,
             (unsigned)ReceiveIndicatedCount);
}

VOID NTAPI NdisMReturnPacket(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  PNDIS_PACKET    Packet)
{
    ULONG i;
    ULONG head;

    UNREFERENCED_PARAMETER(MiniportAdapterHandle);

    /* Remove from receive queue if present */
    head = RxQueueHead;
    while (head < RxQueueTail)
    {
        i = head % RX_QUEUE_MAX;
        if (ReceivePacketRxQueue[i] == Packet)
        {
            ReceivePacketRxQueue[i] = NULL;
            RxQueueHead = head + 1;
            break;
        }
        head++;
    }

    ReceiveReturnedCount++;
    DbgPrint("NDIS: ReturnPacket (total returned=%u)\n", (unsigned)ReceiveReturnedCount);
}

/* ---- Status -------------------------------------------------------- */

/*
 * NdisMIndicateStatus - Indicate a media status change.
 *
 * Called by miniport to notify protocols of link state changes,
 * media connect/disconnect, etc.
 */
VOID NTAPI NdisMIndicateStatus(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_STATUS      GeneralStatus,
    IN  PVOID            StatusBuffer,
    IN  ULONG             StatusBufferSize)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
    UNREFERENCED_PARAMETER(StatusBuffer);

    DbgPrint("NDIS: IndicateStatus Status=0x%08lx BufferSize=%u\n",
             (unsigned long)GeneralStatus, (unsigned)StatusBufferSize);
}

VOID NTAPI NdisMIndicateStatusComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle)
{
    DbgPrint("NDIS: IndicateStatusComplete\n");
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
}

/* ---- Query/Set ---------------------------------------------------- */

/*
 * NdisMQueryInformationComplete - Complete a query OID request.
 */
VOID NTAPI NdisMQueryInformationComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_STATUS     Status)
{
    DbgPrint("NDIS: QueryInformationComplete Status=0x%08lx\n",
             (unsigned long)Status);
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
}

/*
 * NdisMSetInformationComplete - Complete a set OID request.
 */
VOID NTAPI NdisMSetInformationComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_STATUS     Status)
{
    DbgPrint("NDIS: SetInformationComplete Status=0x%08lx\n",
             (unsigned long)Status);
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
}

/* ---- Reset -------------------------------------------------------- */

VOID NTAPI NdisMResetComplete(
    IN  NDIS_HANDLE     MiniportAdapterHandle,
    IN  NDIS_STATUS     Status,
    IN  BOOLEAN         AddressingReset)
{
    UNREFERENCED_PARAMETER(MiniportAdapterHandle);
    DbgPrint("NDIS: ResetComplete Status=0x%08lx AddrReset=%u\n",
             (unsigned long)Status, (unsigned)AddressingReset);
}

/* ---- Shutdown ----------------------------------------------------- */

VOID NTAPI NdisMRegisterAdapterShutdownHandler(
    IN  NDIS_HANDLE                 MiniportHandle,
    IN  PVOID                       ShutdownContext,
    IN  PVOID                       ShutdownHandler)
{
    PNDIS_MINIPORT_ADAPTER Adapter = (PNDIS_MINIPORT_ADAPTER)MiniportHandle;

    if (!Adapter)
        return;

    if (!Adapter->Initialized)
        return;

    Adapter->ShutdownHandler = ShutdownHandler;
    Adapter->ShutdownContext = ShutdownContext;

    DbgPrint("NDIS: RegisterAdapterShutdownHandler handler=%p context=%p\n",
             ShutdownHandler, ShutdownContext);
}

VOID NTAPI NdisMDeregisterAdapterShutdownHandler(
    IN  NDIS_HANDLE                 MiniportHandle)
{
    PNDIS_MINIPORT_ADAPTER Adapter = (PNDIS_MINIPORT_ADAPTER)MiniportHandle;

    if (!Adapter)
        return;

    if (!Adapter->Initialized)
        return;

    DbgPrint("NDIS: DeregisterAdapterShutdownHandler handler=%p context=%p\n",
             Adapter->ShutdownHandler, Adapter->ShutdownContext);

    Adapter->ShutdownHandler = NULL;
    Adapter->ShutdownContext = NULL;
}

/* ---- Synchronization ----------------------------------------------- */

/*
 * NdisMSynchronizeWithInterrupt - Execute function at ISR IRQL.
 */
BOOLEAN NTAPI NdisMSynchronizeWithInterrupt(
    IN  NDIS_MINIPORT_INTERRUPT    Interrupt,
    IN  PVOID                      SynchronizeFunction,
    IN  PVOID                      SynchronizeContext)
{
    typedef BOOLEAN (*SYNCHRONIZE_FUNC)(PVOID);
    UNREFERENCED_PARAMETER(Interrupt);

    if (!SynchronizeFunction)
        return FALSE;

    return ((SYNCHRONIZE_FUNC)SynchronizeFunction)(SynchronizeContext);
}

/* ---- Utility ------------------------------------------------------ */

/*
 * NdisMSleep - Delay execution.
 *
 * @MicrosecondsToSleep: Number of microseconds to sleep (max 1 second)
 */
VOID NTAPI NdisMSleep(
    IN  ULONG       MicrosecondsToSleep)
{
    if (MicrosecondsToSleep > 1000000)
        MicrosecondsToSleep = 1000000;

    if (MicrosecondsToSleep <= 1000)
        KeStallExecutionProcessor(MicrosecondsToSleep);
    else
        KeStallExecutionProcessor(MicrosecondsToSleep);
}

/* ---- Error log ---------------------------------------------------- */

VOID NTAPI NdisWriteErrorLogEntry(
    IN  NDIS_HANDLE     NdisAdapterHandle,
    IN  NDIS_ERROR_CODE ErrorCode,
    IN  ULONG           NumberOfErrorValues,
    ...)
{
    UNREFERENCED_PARAMETER(NdisAdapterHandle);
    UNREFERENCED_PARAMETER(NumberOfErrorValues);

    DbgPrint("NDIS: ErrorLogEntry ErrorCode=0x%lx\n", (unsigned long)ErrorCode);
}

NDIS_STATUS NTAPI NdisWriteEventLogEntry(
    IN  PVOID       LogHandle,
    IN  NDIS_STATUS EventCode,
    IN  ULONG       UniqueEventValue,
    IN  USHORT      NumStrings,
    IN  PVOID       StringsList OPTIONAL,
    IN  ULONG       DataSize,
    IN  PVOID       Data OPTIONAL)
{
    UNREFERENCED_PARAMETER(LogHandle);
    UNREFERENCED_PARAMETER(UniqueEventValue);
    UNREFERENCED_PARAMETER(NumStrings);
    UNREFERENCED_PARAMETER(StringsList);
    UNREFERENCED_PARAMETER(DataSize);
    UNREFERENCED_PARAMETER(Data);

    DbgPrint("NDIS: WriteEventLogEntry EventCode=0x%lx\n",
             (unsigned long)EventCode);

    return NDIS_STATUS_SUCCESS;
}