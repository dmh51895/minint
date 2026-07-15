/*
 * MinNT - wmi/wmi.c
 * Windows Management Instrumentation (WMI) provider infrastructure.
 *
 * WMI is "SQL for the OS's own state". A WMI consumer sends a query
 * (e.g. "SELECT * FROM Win32_Processor") to the WMI service which in
 * turn invokes registered providers. Each provider exposes a set of
 * classes/instances; the query engine walks the predicate against the
 * result set and returns matching instances.
 *
 * This file implements:
 *   - a provider registry
 *   - class/instance definition
 *   - the simple query engine (SELECT ... FROM ... WHERE ... [AND ...])
 *   - built-in providers for Win32_OperatingSystem, Win32_Processor,
 *     Win32_ComputerSystem, Win32_LogicalDisk, and Win32_Process
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define WMI_MAX_PROVIDERS       16
#define WMI_MAX_CLASSES_PER     32
#define WMI_MAX_INSTANCES       256
#define WMI_MAX_PROPS_PER       32
#define WMI_MAX_NAME            128

/* WMI_PROP_TYPE, WMI_PROPERTY, WMI_RESULT are defined in framework.h */

typedef struct _WMI_CLASS {
    CHAR Name[WMI_MAX_NAME];
    PWMI_PROPERTY Properties;
    ULONG PropertyCount;
    ULONG InstanceSize;
    PUCHAR Instances;
    ULONG InstanceCount;
    struct _WMI_CLASS *Next;
} WMI_CLASS, *PWMI_CLASS;

typedef struct _WMI_PROVIDER {
    CHAR Name[WMI_MAX_NAME];
    PWMI_CLASS Classes;
    struct _WMI_PROVIDER *Next;
} WMI_PROVIDER, *PWMI_PROVIDER;

static WMI_PROVIDER *g_Providers = NULL;
static ULONG g_ProviderCount = 0;

/* WMI_PROP_TYPE, WMI_PROPERTY, WMI_RESULT are defined in framework.h */
static VOID WmiSetString(PWMI_CLASS cls, PVOID instance, ULONG propIdx,
                         const CHAR *value)
{
    PUCHAR base = (PUCHAR)instance + cls->Properties[propIdx].Offset;
    ULONG i = 0;
    while (value[i] && i < WMI_MAX_NAME - 1) { base[i] = value[i]; i++; }
    base[i] = 0;
}

static BOOLEAN WmiCmpString(PWMI_CLASS cls, PVOID instance, ULONG propIdx,
                            const CHAR *value)
{
    PUCHAR base = (PUCHAR)instance + cls->Properties[propIdx].Offset;
    ULONG i = 0;
    while (base[i] && value[i]) {
        if (base[i] != value[i]) return FALSE;
        i++;
    }
    return base[i] == value[i];
}

static BOOLEAN WmiCmpUint32(PWMI_CLASS cls, PVOID instance, ULONG propIdx,
                            ULONG value)
{
    PULONG p = (PULONG)((PUCHAR)instance + cls->Properties[propIdx].Offset);
    return *p == value;
}

static BOOLEAN WmiCmpUint64(PWMI_CLASS cls, PVOID instance, ULONG propIdx,
                            ULONGLONG value)
{
    PULONG64 p = (PULONG64)((PUCHAR)instance + cls->Properties[propIdx].Offset);
    return *p == value;
}

static BOOLEAN WmiCmpBool(PWMI_CLASS cls, PVOID instance, ULONG propIdx,
                          BOOLEAN value)
{
    PBOOLEAN p = (PBOOLEAN)((PUCHAR)instance + cls->Properties[propIdx].Offset);
    return (*p ? TRUE : FALSE) == (value ? TRUE : FALSE);
}

/* Find a property by name within a class. */
static LONG WmiFindProp(PWMI_CLASS cls, const CHAR *name)
{
    for (ULONG i = 0; i < cls->PropertyCount; i++) {
        BOOLEAN eq = TRUE;
        for (ULONG j = 0; j < WMI_MAX_NAME; j++) {
            if (cls->Properties[i].Name[j] != name[j]) { eq = FALSE; break; }
            if (name[j] == 0) break;
        }
        if (eq) return (LONG)i;
    }
    return -1;
}

/* Parse "SELECT <prop> FROM <class> WHERE <prop>=<value> [AND <prop>=<value>]". */
typedef struct _WMI_QUERY {
    PWMI_CLASS Class;
    LONG SelProp;
    struct {
        LONG PropIdx;
        BOOLEAN IsUint;
        BOOLEAN IsBool;
        ULONG UintValue;
        ULONGLONG Uint64Value;
        CHAR StrValue[WMI_MAX_NAME];
    } Predicates[4];
    ULONG PredicateCount;
} WMI_QUERY, *PWMI_QUERY;

static NTSTATUS WmiParseQuery(const CHAR *q, PWMI_QUERY out)
{
    /* Skip whitespace, then SELECT */
    ULONG i = 0;
    while (q[i] == ' ' || q[i] == '\t') i++;
    if (!(q[i] == 'S' && q[i+1] == 'E' && q[i+2] == 'L' && q[i+3] == 'E' &&
          q[i+4] == 'C' && q[i+5] == 'T' && q[i+6] == ' '))
        return STATUS_INVALID_PARAMETER;
    i += 7;
    while (q[i] == ' ') i++;
    CHAR sel[WMI_MAX_NAME]; ULONG sl = 0;
    while (q[i] && q[i] != ' ' && sl < WMI_MAX_NAME - 1) sel[sl++] = q[i++];
    sel[sl] = 0;
    while (q[i] == ' ') i++;
    if (!(q[i] == 'F' && q[i+1] == 'R' && q[i+2] == 'O' && q[i+3] == 'M' &&
          q[i+4] == ' ')) return STATUS_INVALID_PARAMETER;
    i += 5;
    while (q[i] == ' ') i++;
    CHAR className[WMI_MAX_NAME]; ULONG cl = 0;
    while (q[i] && q[i] != ' ' && cl < WMI_MAX_NAME - 1) className[cl++] = q[i++];
    className[cl] = 0;

    /* Find the class in any provider. */
    PWMI_PROVIDER prov = g_Providers;
    while (prov) {
        PWMI_CLASS cls = prov->Classes;
        while (cls) {
            BOOLEAN eq = TRUE;
            for (ULONG k = 0; k < WMI_MAX_NAME; k++) {
                if (cls->Name[k] != className[k]) { eq = FALSE; break; }
                if (className[k] == 0) break;
            }
            if (eq) { out->Class = cls; break; }
            cls = cls->Next;
        }
        if (out->Class) break;
        prov = prov->Next;
    }
    if (!out->Class) return STATUS_NOT_FOUND;

    out->SelProp = WmiFindProp(out->Class, sel);
    out->PredicateCount = 0;

    /* Optional WHERE clause. */
    while (q[i] == ' ') i++;
    if (!q[i]) return STATUS_SUCCESS;
    if (!(q[i] == 'W' && q[i+1] == 'H' && q[i+2] == 'E' && q[i+3] == 'R' &&
          q[i+4] == 'E' && q[i+5] == ' '))
        return STATUS_INVALID_PARAMETER;
    i += 6;
    while (q[i] == ' ') i++;
    while (q[i]) {
        if (out->PredicateCount >= 4) return STATUS_INVALID_PARAMETER;
        CHAR pname[WMI_MAX_NAME]; ULONG pl = 0;
        while (q[i] && q[i] != '=' && pl < WMI_MAX_NAME - 1) pname[pl++] = q[i++];
        pname[pl] = 0;
        if (q[i] != '=') return STATUS_INVALID_PARAMETER;
        i++;
        CHAR pval[WMI_MAX_NAME]; ULONG pvl = 0;
        if (q[i] == '"') {
            i++;
            while (q[i] && q[i] != '"' && pvl < WMI_MAX_NAME - 1) pval[pvl++] = q[i++];
            if (q[i] == '"') i++;
        } else {
            while (q[i] && q[i] != ' ' && pvl < WMI_MAX_NAME - 1) pval[pvl++] = q[i++];
        }
        pval[pvl] = 0;

        LONG pidx = WmiFindProp(out->Class, pname);
        if (pidx < 0) return STATUS_NOT_FOUND;
        out->Predicates[out->PredicateCount].PropIdx = pidx;
        if (out->Class->Properties[pidx].Type == WMI_UINT32) {
            out->Predicates[out->PredicateCount].IsUint = TRUE;
            ULONG v = 0;
            for (ULONG k = 0; pval[k]; k++) {
                if (pval[k] < '0' || pval[k] > '9') v = 0;
                else v = v * 10 + (pval[k] - '0');
            }
            out->Predicates[out->PredicateCount].UintValue = v;
        } else if (out->Class->Properties[pidx].Type == WMI_UINT64) {
            out->Predicates[out->PredicateCount].IsUint = TRUE;
            ULONGLONG v = 0;
            for (ULONG k = 0; pval[k]; k++) {
                if (pval[k] < '0' || pval[k] > '9') v = 0;
                else v = v * 10 + (ULONGLONG)(pval[k] - '0');
            }
            out->Predicates[out->PredicateCount].Uint64Value = v;
        } else if (out->Class->Properties[pidx].Type == WMI_BOOLEAN) {
            out->Predicates[out->PredicateCount].IsBool = TRUE;
            out->Predicates[out->PredicateCount].UintValue =
                (pval[0] == 'T' || pval[0] == 't') ? 1 : 0;
        } else {
            for (ULONG k = 0; k < WMI_MAX_NAME; k++) {
                out->Predicates[out->PredicateCount].StrValue[k] = pval[k];
                if (pval[k] == 0) break;
            }
        }
        out->PredicateCount++;
        while (q[i] == ' ') i++;
        if (q[i] == 'A' && q[i+1] == 'N' && q[i+2] == 'D' && q[i+3] == ' ') {
            i += 4;
            while (q[i] == ' ') i++;
        } else {
            break;
        }
    }
    return STATUS_SUCCESS;
}

/* Find or create a provider by name. */
static PWMI_PROVIDER WmiGetOrCreateProvider(const CHAR *name)
{
    PWMI_PROVIDER p = g_Providers;
    while (p) {
        BOOLEAN eq = TRUE;
        for (ULONG i = 0; i < WMI_MAX_NAME; i++) {
            if (p->Name[i] != name[i]) { eq = FALSE; break; }
            if (name[i] == 0) break;
        }
        if (eq) return p;
        p = p->Next;
    }
    if (g_ProviderCount >= WMI_MAX_PROVIDERS) return NULL;
    p = (PWMI_PROVIDER)ExAllocatePool(0, sizeof(WMI_PROVIDER));
    if (!p) return NULL;
    RtlZeroMemory(p, sizeof(*p));
    for (ULONG i = 0; i < WMI_MAX_NAME; i++) {
        p->Name[i] = name[i];
        if (name[i] == 0) break;
    }
    p->Next = g_Providers;
    g_Providers = p;
    g_ProviderCount++;
    return p;
}

/* Register a class definition. */
NTSTATUS NTAPI WmiRegisterClass(const CHAR *ProviderName, const CHAR *ClassName,
                                PWMI_PROPERTY Props, ULONG PropCount,
                                ULONG InstanceSize)
{
    PWMI_PROVIDER prov = WmiGetOrCreateProvider(ProviderName);
    if (!prov) return STATUS_NO_MEMORY;
    PWMI_CLASS cls = (PWMI_CLASS)ExAllocatePool(0, sizeof(WMI_CLASS));
    if (!cls) return STATUS_NO_MEMORY;
    RtlZeroMemory(cls, sizeof(*cls));
    for (ULONG i = 0; i < WMI_MAX_NAME; i++) {
        cls->Name[i] = ClassName[i];
        if (ClassName[i] == 0) break;
    }
    cls->Properties = Props;
    cls->PropertyCount = PropCount;
    cls->InstanceSize = InstanceSize;
    cls->Instances = (PUCHAR)ExAllocatePool(0, InstanceSize * WMI_MAX_INSTANCES);
    if (!cls->Instances) {
        ExFreePool(cls);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(cls->Instances, InstanceSize * WMI_MAX_INSTANCES);
    cls->Next = prov->Classes;
    prov->Classes = cls;
    return STATUS_SUCCESS;
}

/* Add a new instance of a class. */
NTSTATUS NTAPI WmiAddInstance(const CHAR *ClassName, PVOID Data, ULONG Size)
{
    PWMI_PROVIDER prov = g_Providers;
    while (prov) {
        PWMI_CLASS cls = prov->Classes;
        while (cls) {
            BOOLEAN eq = TRUE;
            for (ULONG i = 0; i < WMI_MAX_NAME; i++) {
                if (cls->Name[i] != ClassName[i]) { eq = FALSE; break; }
                if (ClassName[i] == 0) break;
            }
            if (eq) {
                if (cls->InstanceCount >= WMI_MAX_INSTANCES) return STATUS_NO_MEMORY;
                if (Size != cls->InstanceSize) return STATUS_INVALID_PARAMETER;
                RtlCopyMemory(cls->Instances + cls->InstanceCount * cls->InstanceSize,
                              Data, Size);
                cls->InstanceCount++;
                return STATUS_SUCCESS;
            }
            cls = cls->Next;
        }
        prov = prov->Next;
    }
    return STATUS_NOT_FOUND;
}

/* Execute a query and write matching instances into a result buffer. */
NTSTATUS NTAPI WmiQuery(const CHAR *Query, PWMI_RESULT OutResult)
{
    WMI_QUERY q;
    RtlZeroMemory(&q, sizeof(q));
    NTSTATUS s = WmiParseQuery(Query, &q);
    if (!NT_SUCCESS(s)) return s;
    if (!q.Class) return STATUS_NOT_FOUND;
    BOOLEAN *match = (BOOLEAN *)ExAllocatePool(0, q.Class->InstanceCount * sizeof(BOOLEAN));
    if (!match) return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < q.Class->InstanceCount; i++) match[i] = TRUE;
    for (ULONG p = 0; p < q.PredicateCount; p++) {
        for (ULONG i = 0; i < q.Class->InstanceCount; i++) {
            PVOID inst = q.Class->Instances + i * q.Class->InstanceSize;
            BOOLEAN ok = FALSE;
            if (q.Predicates[p].IsBool) {
                ok = WmiCmpBool(q.Class, inst, q.Predicates[p].PropIdx,
                                (BOOLEAN)q.Predicates[p].UintValue);
            } else if (q.Predicates[p].IsUint) {
                if (q.Class->Properties[q.Predicates[p].PropIdx].Type == WMI_UINT64) {
                    ok = WmiCmpUint64(q.Class, inst, q.Predicates[p].PropIdx,
                                      q.Predicates[p].Uint64Value);
                } else {
                    ok = WmiCmpUint32(q.Class, inst, q.Predicates[p].PropIdx,
                                      q.Predicates[p].UintValue);
                }
            } else {
                ok = WmiCmpString(q.Class, inst, q.Predicates[p].PropIdx,
                                  q.Predicates[p].StrValue);
            }
            if (!ok) match[i] = FALSE;
        }
    }
    ULONG count = 0;
    for (ULONG i = 0; i < q.Class->InstanceCount; i++) if (match[i]) count++;
    OutResult->Count = count;
    OutResult->InstanceSize = q.Class->InstanceSize;
    OutResult->Instances = ExAllocatePool(0, count * sizeof(PVOID));
    if (!OutResult->Instances) {
        ExFreePool(match);
        return STATUS_NO_MEMORY;
    }
    ULONG j = 0;
    for (ULONG i = 0; i < q.Class->InstanceCount; i++) {
        if (match[i]) {
            ((PVOID *)OutResult->Instances)[j++] =
                q.Class->Instances + i * q.Class->InstanceSize;
        }
    }
    ExFreePool(match);
    return STATUS_SUCCESS;
}

VOID NTAPI WmiFreeResult(PWMI_RESULT Result)
{
    if (Result && Result->Instances) ExFreePool(Result->Instances);
    if (Result) RtlZeroMemory(Result, sizeof(*Result));
}

/* Register the built-in OS/CPU/Disk/Process providers. */
static WMI_PROPERTY OsProps[6];
static WMI_PROPERTY CpuProps[6];
static WMI_PROPERTY CsProps[4];
static WMI_PROPERTY DiskProps[6];
static WMI_PROPERTY ProcProps[6];

typedef struct _WMI_OS_INST {
    CHAR Caption[64];
    CHAR Version[32];
    CHAR BuildNumber[16];
    ULONG InstallDate;
    ULONG FreePhysical;
    ULONG TotalVisible;
} WMI_OS_INST;

typedef struct _WMI_CPU_INST {
    CHAR Name[64];
    CHAR Manufacturer[32];
    ULONG MaxClock;
    ULONG NumberOfCores;
    ULONG NumberOfLogicalProcessors;
    ULONG Architecture;
} WMI_CPU_INST;

typedef struct _WMI_CS_INST {
    CHAR Name[32];
    CHAR Domain[32];
    ULONG MemorySize;
    ULONG SystemType;
} WMI_CS_INST;

typedef struct _WMI_DISK_INST {
    CHAR DeviceId[16];
    CHAR VolumeName[64];
    CHAR FileSystem[16];
    ULONG Size;
    ULONG FreeSpace;
    ULONG DriveType;
} WMI_DISK_INST;

typedef struct _WMI_PROC_INST {
    CHAR Name[64];
    CHAR ExecutablePath[260];
    ULONG ProcessId;
    ULONG WorkingSetSize;
    ULONG KernelTime;
    ULONG UserTime;
} WMI_PROC_INST;

NTSTATUS NTAPI WmiInit(VOID)
{
    /* OS class */
    OsProps[0].Type = WMI_STRING; RtlCopyMemory(OsProps[0].Name, "Caption", 8);
    OsProps[0].Offset = offsetof(WMI_OS_INST, Caption);
    OsProps[1].Type = WMI_STRING; RtlCopyMemory(OsProps[1].Name, "Version", 8);
    OsProps[1].Offset = offsetof(WMI_OS_INST, Version);
    OsProps[2].Type = WMI_STRING; RtlCopyMemory(OsProps[2].Name, "BuildNumber", 12);
    OsProps[2].Offset = offsetof(WMI_OS_INST, BuildNumber);
    OsProps[3].Type = WMI_UINT32; RtlCopyMemory(OsProps[3].Name, "FreePhysical", 13);
    OsProps[3].Offset = offsetof(WMI_OS_INST, FreePhysical);
    OsProps[4].Type = WMI_UINT32; RtlCopyMemory(OsProps[4].Name, "TotalVisible", 13);
    OsProps[4].Offset = offsetof(WMI_OS_INST, TotalVisible);
    OsProps[5].Type = WMI_UINT32; RtlCopyMemory(OsProps[5].Name, "InstallDate", 12);
    OsProps[5].Offset = offsetof(WMI_OS_INST, InstallDate);
    WmiRegisterClass("CIMV2", "Win32_OperatingSystem", OsProps, 6, sizeof(WMI_OS_INST));

    /* CPU class */
    CpuProps[0].Type = WMI_STRING; RtlCopyMemory(CpuProps[0].Name, "Name", 5);
    CpuProps[0].Offset = offsetof(WMI_CPU_INST, Name);
    CpuProps[1].Type = WMI_STRING; RtlCopyMemory(CpuProps[1].Name, "Manufacturer", 13);
    CpuProps[1].Offset = offsetof(WMI_CPU_INST, Manufacturer);
    CpuProps[2].Type = WMI_UINT32; RtlCopyMemory(CpuProps[2].Name, "MaxClock", 9);
    CpuProps[2].Offset = offsetof(WMI_CPU_INST, MaxClock);
    CpuProps[3].Type = WMI_UINT32; RtlCopyMemory(CpuProps[3].Name, "NumberOfCores", 14);
    CpuProps[3].Offset = offsetof(WMI_CPU_INST, NumberOfCores);
    CpuProps[4].Type = WMI_UINT32; RtlCopyMemory(CpuProps[4].Name, "NumberOfLogicalProcessors", 27);
    CpuProps[4].Offset = offsetof(WMI_CPU_INST, NumberOfLogicalProcessors);
    CpuProps[5].Type = WMI_UINT32; RtlCopyMemory(CpuProps[5].Name, "Architecture", 13);
    CpuProps[5].Offset = offsetof(WMI_CPU_INST, Architecture);
    WmiRegisterClass("CIMV2", "Win32_Processor", CpuProps, 6, sizeof(WMI_CPU_INST));

    /* Computer System class */
    CsProps[0].Type = WMI_STRING; RtlCopyMemory(CsProps[0].Name, "Name", 5);
    CsProps[0].Offset = offsetof(WMI_CS_INST, Name);
    CsProps[1].Type = WMI_STRING; RtlCopyMemory(CsProps[1].Name, "Domain", 7);
    CsProps[1].Offset = offsetof(WMI_CS_INST, Domain);
    CsProps[2].Type = WMI_UINT32; RtlCopyMemory(CsProps[2].Name, "MemorySize", 11);
    CsProps[2].Offset = offsetof(WMI_CS_INST, MemorySize);
    CsProps[3].Type = WMI_UINT32; RtlCopyMemory(CsProps[3].Name, "SystemType", 11);
    CsProps[3].Offset = offsetof(WMI_CS_INST, SystemType);
    WmiRegisterClass("CIMV2", "Win32_ComputerSystem", CsProps, 4, sizeof(WMI_CS_INST));

    /* Logical Disk class */
    DiskProps[0].Type = WMI_STRING; RtlCopyMemory(DiskProps[0].Name, "DeviceId", 9);
    DiskProps[0].Offset = offsetof(WMI_DISK_INST, DeviceId);
    DiskProps[1].Type = WMI_STRING; RtlCopyMemory(DiskProps[1].Name, "VolumeName", 11);
    DiskProps[1].Offset = offsetof(WMI_DISK_INST, VolumeName);
    DiskProps[2].Type = WMI_STRING; RtlCopyMemory(DiskProps[2].Name, "FileSystem", 11);
    DiskProps[2].Offset = offsetof(WMI_DISK_INST, FileSystem);
    DiskProps[3].Type = WMI_UINT32; RtlCopyMemory(DiskProps[3].Name, "Size", 5);
    DiskProps[3].Offset = offsetof(WMI_DISK_INST, Size);
    DiskProps[4].Type = WMI_UINT32; RtlCopyMemory(DiskProps[4].Name, "FreeSpace", 10);
    DiskProps[4].Offset = offsetof(WMI_DISK_INST, FreeSpace);
    DiskProps[5].Type = WMI_UINT32; RtlCopyMemory(DiskProps[5].Name, "DriveType", 10);
    DiskProps[5].Offset = offsetof(WMI_DISK_INST, DriveType);
    WmiRegisterClass("CIMV2", "Win32_LogicalDisk", DiskProps, 6, sizeof(WMI_DISK_INST));

    /* Process class */
    ProcProps[0].Type = WMI_STRING; RtlCopyMemory(ProcProps[0].Name, "Name", 5);
    ProcProps[0].Offset = offsetof(WMI_PROC_INST, Name);
    ProcProps[1].Type = WMI_STRING; RtlCopyMemory(ProcProps[1].Name, "ExecutablePath", 15);
    ProcProps[1].Offset = offsetof(WMI_PROC_INST, ExecutablePath);
    ProcProps[2].Type = WMI_UINT32; RtlCopyMemory(ProcProps[2].Name, "ProcessId", 10);
    ProcProps[2].Offset = offsetof(WMI_PROC_INST, ProcessId);
    ProcProps[3].Type = WMI_UINT32; RtlCopyMemory(ProcProps[3].Name, "WorkingSetSize", 15);
    ProcProps[3].Offset = offsetof(WMI_PROC_INST, WorkingSetSize);
    ProcProps[4].Type = WMI_UINT32; RtlCopyMemory(ProcProps[4].Name, "KernelTime", 11);
    ProcProps[4].Offset = offsetof(WMI_PROC_INST, KernelTime);
    ProcProps[5].Type = WMI_UINT32; RtlCopyMemory(ProcProps[5].Name, "UserTime", 9);
    ProcProps[5].Offset = offsetof(WMI_PROC_INST, UserTime);
    WmiRegisterClass("CIMV2", "Win32_Process", ProcProps, 6, sizeof(WMI_PROC_INST));

    /* Populate with one default OS instance */
    WMI_OS_INST os = { 0 };
    RtlCopyMemory(os.Caption, "MinNT", 6);
    RtlCopyMemory(os.Version, "6.0", 4);
    RtlCopyMemory(os.BuildNumber, "6000", 5);
    os.FreePhysical = 64 * 1024 * 1024;
    os.TotalVisible = 64ULL * 1024 * 1024 * 1024;
    WmiAddInstance("Win32_OperatingSystem", &os, sizeof(os));

    WMI_CPU_INST cpu = { 0 };
    RtlCopyMemory(cpu.Name, "MinNT Virtual CPU", 18);
    RtlCopyMemory(cpu.Manufacturer, "MinNT", 6);
    cpu.MaxClock = 3000;
    cpu.NumberOfCores = 4;
    cpu.NumberOfLogicalProcessors = 4;
    cpu.Architecture = 9; /* x64 */
    WmiAddInstance("Win32_Processor", &cpu, sizeof(cpu));

    WMI_CS_INST cs = { 0 };
    RtlCopyMemory(cs.Name, "MININTPC", 9);
    RtlCopyMemory(cs.Domain, "WORKGROUP", 10);
    cs.MemorySize = 64 * 1024 * 1024 * 1024;
    cs.SystemType = 1;
    WmiAddInstance("Win32_ComputerSystem", &cs, sizeof(cs));

    WMI_DISK_INST d = { 0 };
    RtlCopyMemory(d.DeviceId, "C:", 3);
    RtlCopyMemory(d.VolumeName, "MinNT", 6);
    RtlCopyMemory(d.FileSystem, "FAT32", 6);
    d.Size = 16 * 1024 * 1024 * 1024;
    d.FreeSpace = 8 * 1024 * 1024 * 1024;
    d.DriveType = 3;
    WmiAddInstance("Win32_LogicalDisk", &d, sizeof(d));

    DbgPrint("WMI: provider database initialized with 5 classes\n");
    return STATUS_SUCCESS;
}
