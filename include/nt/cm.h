/*
 * MinNT - cm.h
 * Configuration Manager (Registry): in-memory hive with key/value nodes.
 * Minimal NT 6.x registry — enough for SMSS/CSRSS to read boot config.
 */

#ifndef _CM_H_
#define _CM_H_

#include <nt/ntdef.h>

/* ---- Registry value types ------------------------------------------------ */

#define REG_NONE                0
#define REG_SZ                  1
#define REG_EXPAND_SZ           2
#define REG_BINARY              3
#define REG_DWORD               4
#define REG_DWORD_BIG_ENDIAN    5
#define REG_LINK                6
#define REG_MULTI_SZ            7
#define REG_QWORD               11

/* ---- Access rights ------------------------------------------------------- */

#define KEY_QUERY_VALUE         0x0001
#define KEY_SET_VALUE           0x0002
#define KEY_CREATE_SUB_KEY      0x0004
#define KEY_ENUMERATE_SUB_KEYS  0x0010
#define KEY_READ                0x20019
#define KEY_WRITE               0x20006
#define KEY_ALL_ACCESS          0xF003F

/* ---- Create/open options ------------------------------------------------- */

#define REG_OPTION_NON_VOLATILE 0
#define REG_OPTION_VOLATILE      1
#define REG_CREATED_NEW_KEY      0x00000001
#define REG_OPENED_EXISTING_KEY  0x00000002

/* ---- Node types ---------------------------------------------------------- */

#define CM_NODE_KEY     0x6B794B43  /* 'CKyk' */
#define CM_NODE_VALUE   0x6C617643  /* 'Cval' */

/* ---- Key node ------------------------------------------------------------ */

typedef struct _CM_KEY_NODE {
    ULONG               Type;           /* CM_NODE_KEY */
    UNICODE_STRING      Name;
    struct _CM_KEY_NODE *Parent;
    struct _CM_KEY_NODE *ChildHead;     /* first subkey */
    struct _CM_KEY_NODE *NextSibling;   /* next sibling */
    LIST_ENTRY          ValueListHead;  /* values under this key */
    ULONG               SubKeyCount;
    ULONG               ValueCount;
} CM_KEY_NODE, *PCM_KEY_NODE;

/* ---- Value node ---------------------------------------------------------- */

typedef struct _CM_KEY_VALUE {
    ULONG               Type;           /* CM_NODE_VALUE */
    UNICODE_STRING      Name;
    ULONG               DataType;       /* REG_SZ, REG_DWORD, etc. */
    ULONG               DataLength;
    PVOID               Data;           /* pooled buffer */
    LIST_ENTRY          ValueListEntry; /* link into CM_KEY_NODE.ValueListHead */
} CM_KEY_VALUE, *PCM_KEY_VALUE;

/* ---- API ----------------------------------------------------------------- */

NTSTATUS NTAPI CmInitSystem(VOID);

NTSTATUS NTAPI CmCreateKey(PUNICODE_STRING KeyName,
                           ULONG CreateOptions,
                           PCM_KEY_NODE *OutKey);

NTSTATUS NTAPI CmOpenKey(PUNICODE_STRING KeyName,
                         ULONG DesiredAccess,
                         PCM_KEY_NODE *OutKey);

NTSTATUS NTAPI CmSetValue(PCM_KEY_NODE Key,
                          PUNICODE_STRING ValueName,
                          ULONG DataType,
                          PVOID Data,
                          ULONG DataLength);

NTSTATUS NTAPI CmQueryValue(PCM_KEY_NODE Key,
                            PUNICODE_STRING ValueName,
                            PULONG OutDataType,
                            PVOID OutData,
                            ULONG OutDataLength,
                            PULONG OutActualLength);

NTSTATUS NTAPI CmEnumerateSubKey(PCM_KEY_NODE Key,
                                 ULONG Index,
                                 PUNICODE_STRING OutName);

NTSTATUS NTAPI CmEnumerateValue(PCM_KEY_NODE Key,
                                ULONG Index,
                                PUNICODE_STRING OutName,
                                PULONG OutDataType,
                                PVOID OutData,
                                ULONG OutDataLength);

PCM_KEY_NODE NTAPI CmGetRootKey(VOID);

#endif /* _CM_H_ */
